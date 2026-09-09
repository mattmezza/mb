// SPDX-License-Identifier: BSD-3-Clause
#include "mb/browser/user_config_state.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string_view>
#include <utility>

#include "mb/browser/environment_paths.h"

namespace mb {
namespace {

constexpr char kConfigFilename[] = "config.toml";
constexpr char kStateFilename[] = "state.toml";
constexpr size_t kStateLimit = 512;
constexpr int kTemporaryNameAttempts = 32;

bool Fail(UserConfigStateError* error,
          std::string key,
          std::string message,
          int system_errno = 0) {
  *error = {std::move(key), std::move(message), system_errno};
  return false;
}

bool IsPortableName(std::string_view name) {
  if (name.empty() || name.size() > 64) {
    return false;
  }
  const auto alnum = [](unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9');
  };
  if (!alnum(name.front())) {
    return false;
  }
  for (const unsigned char ch : name) {
    if (!alnum(ch) && ch != '_' && ch != '-') {
      return false;
    }
  }
  return true;
}

std::string Join(std::string base, std::string_view component) {
  while (base.size() > 1 && base.back() == '/') {
    base.pop_back();
  }
  return base + "/" + std::string(component);
}

bool ResolveDirectories(const UserConfigPaths& paths,
                        const std::string& home,
                        EnvironmentDirectories* resolved,
                        UserConfigStateError* error) {
  EnvironmentPathError path_error;
  if (!ResolveEnvironmentPaths(paths.config_file, home,
                               {{"config", paths.config_directory},
                                {"state", paths.state_directory},
                                {"default", paths.default_environment_root}},
                               resolved, &path_error)) {
    return Fail(error, path_error.key, path_error.message,
                path_error.system_errno);
  }
  return true;
}

bool AuditDirectories(const UserConfigPaths& paths,
                      const std::string& home,
                      EnvironmentDirectories* resolved,
                      UserConfigStateError* error) {
  EnvironmentPathError path_error;
  if (!AuditEnvironmentPaths(paths.config_file, home,
                             {{"config", paths.config_directory},
                              {"state", paths.state_directory},
                              {"default", paths.default_environment_root}},
                             resolved, &path_error)) {
    return Fail(error, path_error.key, path_error.message,
                path_error.system_errno);
  }
  return true;
}

bool Prepare(const UserConfigPaths& paths,
             const std::string& home,
             std::string_view selected,
             PreparedEnvironmentRoot* prepared,
             UserConfigStateError* error) {
  EnvironmentDirectories directories;
  if (!ResolveDirectories(paths, home, &directories, error)) {
    return false;
  }
  EnvironmentPathError path_error;
  if (!PrepareEnvironmentRoot(paths.config_file, home, directories,
                              std::string(selected), prepared, &path_error)) {
    return Fail(error, path_error.key, path_error.message,
                path_error.system_errno);
  }
  return true;
}

bool WriteAll(int fd, std::string_view text, UserConfigStateError* error) {
  while (!text.empty()) {
    const ssize_t written = write(fd, text.data(), text.size());
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return Fail(error, "<file>", "Cannot write file", errno);
    }
    if (written == 0) {
      return Fail(error, "<file>", "Cannot write file", EIO);
    }
    text.remove_prefix(static_cast<size_t>(written));
  }
  return true;
}

bool CheckPrivateRegular(int directory_fd,
                         const char* filename,
                         UserConfigStateError* error) {
  struct stat st;
  if (fstatat(directory_fd, filename, &st, AT_SYMLINK_NOFOLLOW) != 0) {
    return Fail(error, "<file>", "Cannot inspect file", errno);
  }
  if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
      (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
    return Fail(error, "<file>",
                "File must be a private regular file owned by this user");
  }
  return true;
}

std::string TomlString(std::string_view value) {
  std::string quoted = "\"";
  for (const char ch : value) {
    if (ch == '\\' || ch == '"') {
      quoted += '\\';
    }
    quoted += ch;
  }
  return quoted + '"';
}

std::string DefaultConfigTemplate(const UserConfigPaths& paths) {
  return "schema_version = 1\n"
         "\n"
         "[app]\n"
         "default_environment = \"personal\"\n"
         "restore_last_environment = true\n"
         "\n"
         "[environments.personal]\n"
         "data_directory = " +
         TomlString(paths.default_environment_root) + "\n";
}

std::string StateText(std::string_view environment) {
  return "schema_version = 1\nlast_environment = \"" +
         std::string(environment) + "\"\n";
}

bool ParseState(std::string_view text,
                std::optional<std::string>* remembered,
                UserConfigStateError* error) {
  constexpr std::string_view kPrefix =
      "schema_version = 1\nlast_environment = \"";
  if (!text.starts_with(kPrefix) || !text.ends_with("\"\n")) {
    return Fail(error, "state.toml", "State file has an unsupported schema");
  }
  const std::string value(
      text.substr(kPrefix.size(), text.size() - kPrefix.size() - 2));
  if (!IsPortableName(value)) {
    return Fail(error, "state.toml",
                "State file has an invalid remembered environment");
  }
  *remembered = value;
  return true;
}

bool FinishCreatedFile(int directory_fd,
                       int fd,
                       const char* filename,
                       std::string_view contents,
                       std::string_view noun,
                       UserConfigStateError* error) {
  if (!WriteAll(fd, contents, error)) {
    const int number = error->system_errno;
    close(fd);
    unlinkat(directory_fd, filename, 0);
    return Fail(error, std::string(filename),
                "Cannot write " + std::string(noun), number);
  }
  if (fsync(fd) != 0) {
    const int number = errno;
    close(fd);
    unlinkat(directory_fd, filename, 0);
    return Fail(error, std::string(filename),
                "Cannot synchronize " + std::string(noun), number);
  }
  if (close(fd) != 0) {
    const int number = errno;
    unlinkat(directory_fd, filename, 0);
    return Fail(error, std::string(filename),
                "Cannot close " + std::string(noun), number);
  }
  return true;
}

}  // namespace

bool ResolveUserConfigPaths(const std::string& home,
                            const std::optional<std::string>& xdg_config_home,
                            const std::optional<std::string>& xdg_state_home,
                            const std::optional<std::string>& xdg_data_home,
                            const std::string& profile_directory,
                            UserConfigPaths* paths,
                            UserConfigStateError* error) {
  if (!paths || !error) {
    return false;
  }
  if (!IsPortableName(profile_directory)) {
    return Fail(error, "profile_directory",
                "Profile directory name must be portable");
  }

  if (xdg_config_home && !xdg_config_home->empty() &&
      xdg_config_home->front() != '/') {
    return Fail(error, "XDG_CONFIG_HOME", "XDG_CONFIG_HOME must be absolute");
  }
  if (xdg_state_home && !xdg_state_home->empty() &&
      xdg_state_home->front() != '/') {
    return Fail(error, "XDG_STATE_HOME", "XDG_STATE_HOME must be absolute");
  }
  if (xdg_data_home && !xdg_data_home->empty() &&
      xdg_data_home->front() != '/') {
    return Fail(error, "XDG_DATA_HOME", "XDG_DATA_HOME must be absolute");
  }
  const std::string config_base = xdg_config_home && !xdg_config_home->empty()
                                      ? *xdg_config_home
                                      : Join(home, ".config");
  const std::string state_base = xdg_state_home && !xdg_state_home->empty()
                                     ? *xdg_state_home
                                     : Join(Join(home, ".local"), "state");
  const std::string data_base = xdg_data_home && !xdg_data_home->empty()
                                    ? *xdg_data_home
                                    : Join(Join(home, ".local"), "share");
  UserConfigPaths candidate;
  candidate.config_directory = Join(config_base, profile_directory);
  candidate.config_file = Join(candidate.config_directory, kConfigFilename);
  candidate.state_directory = Join(state_base, profile_directory);
  candidate.state_file = Join(candidate.state_directory, kStateFilename);
  candidate.default_environment_root =
      Join(Join(data_base, profile_directory), "environments/personal");

  EnvironmentDirectories resolved;
  if (!ResolveDirectories(candidate, home, &resolved, error)) {
    return false;
  }
  candidate.config_directory = resolved.at("config");
  candidate.state_directory = resolved.at("state");
  candidate.config_file = Join(candidate.config_directory, kConfigFilename);
  candidate.state_file = Join(candidate.state_directory, kStateFilename);
  candidate.default_environment_root = resolved.at("default");
  *paths = std::move(candidate);
  return true;
}

bool EnsureDefaultConfig(const UserConfigPaths& paths,
                         const std::string& home,
                         bool* created,
                         UserConfigStateError* error) {
  if (!created || !error) {
    return false;
  }
  *created = false;
  PreparedEnvironmentRoot config_directory;
  if (!Prepare(paths, home, "config", &config_directory, error)) {
    return false;
  }

  const int fd =
      openat(config_directory.directory_fd(), kConfigFilename,
             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    if (errno != EEXIST) {
      return Fail(error, "config.toml", "Cannot create default configuration",
                  errno);
    }
    return CheckPrivateRegular(config_directory.directory_fd(), kConfigFilename,
                               error);
  }
  if (!FinishCreatedFile(config_directory.directory_fd(), fd, kConfigFilename,
                         DefaultConfigTemplate(paths), "default configuration",
                         error)) {
    return false;
  }
  if (fsync(config_directory.directory_fd()) != 0) {
    return Fail(error, "config.toml",
                "Cannot synchronize configuration directory", errno);
  }
  *created = true;
  return true;
}

bool ReadRememberedEnvironment(const UserConfigPaths& paths,
                               const std::string& home,
                               std::optional<std::string>* remembered,
                               UserConfigStateError* error) {
  if (!remembered || !error) {
    return false;
  }
  remembered->reset();

  EnvironmentDirectories resolved;
  if (!AuditDirectories(paths, home, &resolved, error)) {
    return false;
  }
  const std::string state_file = Join(resolved.at("state"), kStateFilename);
  const int fd =
      open(state_file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) {
    if (errno == ENOENT) {
      return true;
    }
    return Fail(error, "state.toml", "Cannot open state file", errno);
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    const int number = errno;
    close(fd);
    return Fail(error, "state.toml", "Cannot inspect state file", number);
  }
  if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
      (st.st_mode & (S_IRWXG | S_IRWXO)) != 0 || st.st_size < 0 ||
      static_cast<size_t>(st.st_size) > kStateLimit) {
    close(fd);
    return Fail(
        error, "state.toml",
        "State file must be a private regular file of at most 512 bytes");
  }
  std::string text;
  char buffer[128];
  while (true) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      const int number = errno;
      close(fd);
      return Fail(error, "state.toml", "Cannot read state file", number);
    }
    if (count == 0) {
      break;
    }
    if (text.size() + static_cast<size_t>(count) > kStateLimit) {
      close(fd);
      return Fail(error, "state.toml", "State file exceeds 512 bytes");
    }
    text.append(buffer, static_cast<size_t>(count));
  }
  close(fd);
  return ParseState(text, remembered, error);
}

bool WriteRememberedEnvironment(const UserConfigPaths& paths,
                                const std::string& home,
                                const std::string& environment,
                                UserConfigStateError* error) {
  if (!error) {
    return false;
  }
  if (!IsPortableName(environment)) {
    return Fail(error, "last_environment",
                "Remembered environment must be portable");
  }

  PreparedEnvironmentRoot state_directory;
  if (!Prepare(paths, home, "state", &state_directory, error)) {
    return false;
  }

  std::string temporary;
  int fd = -1;
  for (int attempt = 0; attempt != kTemporaryNameAttempts; ++attempt) {
    temporary = ".state.toml." + std::to_string(getpid()) + "." +
                std::to_string(attempt) + ".tmp";
    fd = openat(state_directory.directory_fd(), temporary.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
      break;
    }
    if (errno != EEXIST) {
      return Fail(error, "state.toml", "Cannot create temporary state file",
                  errno);
    }
  }
  if (fd < 0) {
    return Fail(error, "state.toml", "Cannot allocate temporary state file",
                EEXIST);
  }
  if (!FinishCreatedFile(state_directory.directory_fd(), fd, temporary.c_str(),
                         StateText(environment), "temporary state file",
                         error)) {
    return false;
  }
  if (renameat(state_directory.directory_fd(), temporary.c_str(),
               state_directory.directory_fd(), kStateFilename) != 0) {
    const int number = errno;
    unlinkat(state_directory.directory_fd(), temporary.c_str(), 0);
    return Fail(error, "state.toml", "Cannot replace state file", number);
  }
  if (fsync(state_directory.directory_fd()) != 0) {
    return Fail(error, "state.toml", "Cannot synchronize state directory",
                errno);
  }
  return true;
}

}  // namespace mb
