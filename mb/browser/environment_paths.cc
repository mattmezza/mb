#include "mb/browser/environment_paths.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string_view>
#include <utility>
#include <vector>

namespace mb {
namespace {

class Fd {
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  ~Fd() { if (fd_ >= 0) close(fd_); }
  int get() const { return fd_; }
  int release() { return std::exchange(fd_, -1); }
  void reset(int fd) { if (fd_ >= 0) close(fd_); fd_ = fd; }
 private:
  int fd_;
};

std::string Key(const std::string& name) {
  return "environments." + name + ".data_directory";
}

bool Fail(EnvironmentPathError* error, EnvironmentPathErrorCode code,
          const std::string& key, const std::string& message, int number = 0) {
  *error = {code, key, message, number};
  return false;
}

// Strict UTF-8 scalar decoding, including rejection of overlong sequences,
// surrogate code points and C0/C1 control characters (including NUL).
bool ValidText(std::string_view text) {
  if (text.empty()) return false;
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = text[i++];
    unsigned value = 0;
    unsigned minimum = 0;
    unsigned extra = 0;
    if (lead < 0x80) value = lead;
    else if (lead >= 0xc2 && lead <= 0xdf) {
      value = lead & 0x1f; extra = 1; minimum = 0x80;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      value = lead & 0x0f; extra = 2; minimum = 0x800;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      value = lead & 0x07; extra = 3; minimum = 0x10000;
    } else return false;
    if (i + extra > text.size()) return false;
    while (extra--) {
      const unsigned char next = text[i++];
      if ((next & 0xc0) != 0x80) return false;
      value = (value << 6) | (next & 0x3f);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff) || value < 0x20 ||
        (value >= 0x7f && value <= 0x9f)) return false;
  }
  return true;
}

std::vector<std::string> Components(const std::string& path) {
  std::vector<std::string> result;
  size_t start = 0;
  while (start < path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    if (end != start) result.push_back(path.substr(start, end - start));
    start = end + 1;
  }
  return result;
}

bool Normalize(const std::string& path, const std::string& key,
               std::string* result, EnvironmentPathError* error) {
  if (!ValidText(path) || path[0] != '/')
    return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                "Expected a nonempty absolute UTF-8 path without controls");
  result->clear();
  for (const auto& part : Components(path)) {
    if (part == "..")
      return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                  "Parent traversal ('..') is not permitted");
    if (part == ".") continue;
    if (part.size() > NAME_MAX)
      return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                  "Path component exceeds NAME_MAX (" + std::to_string(NAME_MAX) + " bytes)");
    // Chromium reopens the root by absolute path, even though our descriptor
    // walk could reach deeper paths. Reserve one byte for the terminating NUL.
    if (result->size() + 1 + part.size() >= PATH_MAX)
      return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                  "Normalized absolute path must be shorter than PATH_MAX (" +
                      std::to_string(PATH_MAX) + " bytes including NUL)");
    *result += "/" + part;
  }
  if (result->empty()) *result = "/";
  return true;
}

bool Prefix(const std::string& a, const std::string& b) {
  return a == b || (b.size() > a.size() && b.compare(0, a.size(), a) == 0 &&
                    b[a.size()] == '/');
}

struct Node {
  dev_t device;
  ino_t inode;
  // Remaining lexical path identifies roots beneath a shared existing inode,
  // including missing children beneath bind-mounted aliases.
  std::string remainder;
};
using Trace = std::vector<Node>;

bool CheckDirectory(int fd, bool final, const std::string& key,
                    struct stat* st, EnvironmentPathError* error) {
  if (fstat(fd, st) < 0)
    return Fail(error, EnvironmentPathErrorCode::kSystemError, key,
                "Cannot inspect opened directory", errno);
  const uid_t uid = geteuid();
  if (!S_ISDIR(st->st_mode))
    return Fail(error, EnvironmentPathErrorCode::kUnsafeFilesystem, key,
                "Path component is not a directory");
  if (final) {
    if (st->st_uid != uid || (st->st_mode & 077) != 0 ||
        (st->st_mode & 0700) != 0700)
      return Fail(error, EnvironmentPathErrorCode::kUnsafeFilesystem, key,
                  "Root must belong to the current user with owner rwx and no group/other access");
  } else {
    const bool root_sticky = st->st_uid == 0 && (st->st_mode & S_ISVTX);
    if ((st->st_uid != 0 && st->st_uid != uid) ||
        ((st->st_mode & 022) != 0 && !root_sticky))
      return Fail(error, EnvironmentPathErrorCode::kUnsafeFilesystem, key,
                  "Ancestor must belong to root/current user and resist replacement by other users");
  }
  return true;
}

bool Walk(const std::string& path, const std::string& key, bool create,
          Trace* trace, int* final_fd, EnvironmentPathError* error) {
  constexpr int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
  Fd current(open("/", flags));
  if (current.get() < 0)
    return Fail(error, EnvironmentPathErrorCode::kSystemError, key,
                "Cannot open filesystem root", errno);
  const auto parts = Components(path);
  std::string remainder = path.substr(1);
  for (size_t i = 0; i <= parts.size(); ++i) {
    struct stat st;
    if (!CheckDirectory(current.get(), i == parts.size(), key, &st, error))
      return false;
    if (trace) trace->push_back({st.st_dev, st.st_ino, remainder});
    if (i == parts.size()) {
      if (final_fd) *final_fd = current.release();
      return true;
    }
    int next = openat(current.get(), parts[i].c_str(), flags);
    if (next < 0 && errno == ENOENT) {
      if (!create) return true;
      // EEXIST is expected when two launches prepare the same root. The open
      // and fstat below validate the winner; no check-then-open path lookup.
      if (mkdirat(current.get(), parts[i].c_str(), 0700) < 0 && errno != EEXIST)
        return Fail(error, EnvironmentPathErrorCode::kSystemError, key,
                    "Cannot create private directory component: " + parts[i], errno);
      next = openat(current.get(), parts[i].c_str(), flags);
    }
    if (next < 0) {
      const int number = errno;
      return Fail(error, number == ELOOP || number == ENOTDIR || number == EACCES
                             ? EnvironmentPathErrorCode::kUnsafeFilesystem
                             : EnvironmentPathErrorCode::kSystemError,
                  key, "Cannot securely open directory component (symlinks forbidden): " + parts[i],
                  number);
    }
    current.reset(next);
    const size_t slash = remainder.find('/');
    remainder = slash == std::string::npos ? "" : remainder.substr(slash + 1);
  }
  return false;
}

bool RemaindersOverlap(const std::string& a, const std::string& b) {
  return a.empty() || b.empty() || Prefix(a, b) || Prefix(b, a);
}

}  // namespace

bool ResolveEnvironmentPaths(const std::string& config_file,
                             const std::string& home,
                             const EnvironmentDirectories& directories,
                             EnvironmentDirectories* resolved,
                             EnvironmentPathError* error) {
  *error = {};
  resolved->clear();
  EnvironmentDirectories result;
  std::string config;
  std::string home_path;
  if (!Normalize(config_file, "config_file", &config, error) ||
      !Normalize(home, "home", &home_path, error)) return false;
  if (config == "/" || config_file.back() == '/')
    return Fail(error, EnvironmentPathErrorCode::kInvalidPath, "config_file",
                "Expected the absolute configuration file path");
  const std::string parent = config.substr(0, config.rfind('/'));
  for (const auto& [name, raw] : directories) {
    const std::string key = Key(name);
    if (!ValidText(name) || !ValidText(raw))
      return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                  "Environment name and directory must be nonempty UTF-8 without controls");
    std::string expanded;
    if (raw[0] == '~') {
      if (!raw.starts_with("~/"))
        return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                    "Only ~/ home expansion is supported");
      expanded = home_path + raw.substr(1);
    } else if (raw[0] == '/') expanded = raw;
    else expanded = parent + "/" + raw;
    std::string canonical;
    if (!Normalize(expanded, key, &canonical, error)) return false;
    if (canonical == "/")
      return Fail(error, EnvironmentPathErrorCode::kInvalidPath, key,
                  "Filesystem root cannot be an environment directory");
    for (const auto& [other, other_path] : result) {
      if (Prefix(canonical, other_path) || Prefix(other_path, canonical))
        return Fail(error, EnvironmentPathErrorCode::kOverlappingRoots, key,
                    "Directory aliases or overlaps " + Key(other));
    }
    result.emplace(name, std::move(canonical));
  }
  *resolved = std::move(result);
  return true;
}

PreparedEnvironmentRoot::~PreparedEnvironmentRoot() {
  if (fd_ >= 0) close(fd_);
}
PreparedEnvironmentRoot::PreparedEnvironmentRoot(PreparedEnvironmentRoot&& other) noexcept
    : path_(std::move(other.path_)), fd_(std::exchange(other.fd_, -1)) {}
PreparedEnvironmentRoot& PreparedEnvironmentRoot::operator=(PreparedEnvironmentRoot&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) close(fd_);
    path_ = std::move(other.path_);
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

bool AuditEnvironmentPaths(const std::string& config_file,
                           const std::string& home,
                           const EnvironmentDirectories& directories,
                           EnvironmentDirectories* resolved,
                           EnvironmentPathError* error) {
  resolved->clear();
  EnvironmentDirectories result;
  if (!ResolveEnvironmentPaths(config_file, home, directories, &result, error))
    return false;
  std::map<std::string, Trace> traces;
  for (const auto& [name, path] : result) {
    Trace trace;
    if (!Walk(path, Key(name), false, &trace, nullptr, error)) return false;
    for (const auto& [other, other_trace] : traces)
      for (const auto& node : trace)
        for (const auto& other_node : other_trace)
          if (node.device == other_node.device && node.inode == other_node.inode &&
              RemaindersOverlap(node.remainder, other_node.remainder))
            return Fail(error, EnvironmentPathErrorCode::kOverlappingRoots, Key(name),
                        "Filesystem directory aliases or overlaps " + Key(other));
    traces.emplace(name, std::move(trace));
  }
  *resolved = std::move(result);
  return true;
}

bool PrepareEnvironmentRoot(const std::string& config_file,
                            const std::string& home,
                            const EnvironmentDirectories& directories,
                            const std::string& selected,
                            PreparedEnvironmentRoot* prepared,
                            EnvironmentPathError* error) {
  *prepared = PreparedEnvironmentRoot();
  EnvironmentDirectories resolved;
  if (!AuditEnvironmentPaths(config_file, home, directories, &resolved, error))
    return false;
  const auto chosen = resolved.find(selected);
  if (chosen == resolved.end())
    return Fail(error, EnvironmentPathErrorCode::kUnknownEnvironment, Key(selected),
                "Selected environment does not exist");
  int fd = -1;
  if (!Walk(chosen->second, Key(selected), true, nullptr, &fd, error)) return false;
  prepared->fd_ = fd;
  prepared->path_ = chosen->second;
  return true;
}

}  // namespace mb
