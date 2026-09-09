// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_BROWSER_USER_CONFIG_STATE_H_
#define MB_BROWSER_USER_CONFIG_STATE_H_

#include <optional>
#include <string>

namespace mb {

struct UserConfigPaths {
  std::string config_directory;
  std::string config_file;
  std::string state_directory;
  std::string state_file;
  std::string default_environment_root;
};

struct UserConfigStateError {
  std::string key;
  std::string message;
  int system_errno = 0;
};

// Resolves XDG paths without reading environment variables. Unset XDG values
// are represented by nullopt and fall back to HOME/.config and
// HOME/.local/state and HOME/.local/share. All inputs are checked through the
// existing path policy.
bool ResolveUserConfigPaths(const std::string& home,
                            const std::optional<std::string>& xdg_config_home,
                            const std::optional<std::string>& xdg_state_home,
                            const std::optional<std::string>& xdg_data_home,
                            const std::string& profile_directory,
                            UserConfigPaths* paths,
                            UserConfigStateError* error);

// Creates config.toml exactly once, mode 0600, in a no-symlink 0700 directory.
// Existing files are never replaced. The template contains a usable personal
// environment but creates no environment root.
bool EnsureDefaultConfig(const UserConfigPaths& paths,
                         const std::string& home,
                         bool* created,
                         UserConfigStateError* error);

// Reads the versioned local selection without creating the state directory.
// A missing state file returns success with nullopt. Invalid state is reported
// to the caller, which can warn and fall back to the configured default.
bool ReadRememberedEnvironment(const UserConfigPaths& paths,
                               const std::string& home,
                               std::optional<std::string>* remembered,
                               UserConfigStateError* error);

// Atomically replaces the versioned state file after safely preparing only the
// state directory. The environment name must match the configuration schema.
bool WriteRememberedEnvironment(const UserConfigPaths& paths,
                                const std::string& home,
                                const std::string& environment,
                                UserConfigStateError* error);

}  // namespace mb

#endif  // MB_BROWSER_USER_CONFIG_STATE_H_
