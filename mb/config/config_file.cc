#include "mb/config/config_file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

namespace mb::config {

ConfigFileResult LoadConfigFile(const std::string& filename) {
  ConfigFileResult result;
  result.filename = filename;
  auto fail = [&](const std::string& message) {
    result.parsed.errors.push_back({result.filename, "<file>", message, 0});
    return result;
  };
  if (filename.empty() || filename.find('\0') != std::string::npos)
    return fail("Expected a nonempty filename without NUL");
  char* canonical = realpath(filename.c_str(), nullptr);
  if (!canonical)
    return fail(std::string("Cannot resolve configuration file: ") + std::strerror(errno));
  result.filename = canonical;
  std::free(canonical);
  // O_NONBLOCK ensures a replaced file cannot make startup wait on a FIFO.
  const int fd = open(result.filename.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0)
    return fail(std::string("Cannot open configuration file: ") + std::strerror(errno));
  struct stat st;
  if (fstat(fd, &st) < 0) {
    const int number = errno;
    close(fd);
    return fail(std::string("Cannot inspect configuration file: ") + std::strerror(number));
  }
  constexpr size_t limit = 1024 * 1024;
  if (!S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<uintmax_t>(st.st_size) > limit) {
    close(fd);
    return fail("Configuration must be a regular file of at most 1 MiB");
  }
  std::string text;
  text.reserve(static_cast<size_t>(st.st_size));
  char buffer[8192];
  while (true) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR)
        continue;
      const int number = errno;
      close(fd);
      return fail(std::string("Cannot read configuration file: ") + std::strerror(number));
    }
    if (!count)
      break;
    if (text.size() + static_cast<size_t>(count) > limit) {
      close(fd);
      return fail("Configuration grew beyond the 1 MiB limit while reading");
    }
    text.append(buffer, static_cast<size_t>(count));
  }
  close(fd);
  result.parsed = ParseConfig(text, result.filename);
  return result;
}

}  // namespace mb::config
