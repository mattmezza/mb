// Product argument normalization runs before Chromium CommandLine::Init.
#ifndef MB_APP_STARTUP_ARGUMENTS_H_
#define MB_APP_STARTUP_ARGUMENTS_H_

#include <optional>
#include <string>
#include <vector>

namespace mb {

struct StartupArguments {
  // Owned backing strings; callers must retain them while using argv pointers.
  // Product selectors are normalized to --key=value. Every other token keeps
  // its original position and spelling, including the -- terminator.
  std::vector<std::string> argv;
  std::optional<std::string> config_path;
  std::optional<std::string> environment;
  bool has_user_data_dir = false;
};

struct StartupArgumentsResult {
  std::optional<StartupArguments> value;
  std::string error;
};

// No file I/O, environment-variable access, global state, or shell invocation.
// Errors return no partial result. Unknown Chromium switches pass unchanged.
// Root conflicts are enforced by the later browser-only startup service:
// child processes legitimately receive Chromium's own --user-data-dir.
StartupArgumentsResult NormalizeStartupArguments(
    const std::vector<std::string>& argv);

}  // namespace mb
#endif  // MB_APP_STARTUP_ARGUMENTS_H_
