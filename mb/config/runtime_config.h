#ifndef MB_CONFIG_RUNTIME_CONFIG_H_
#define MB_CONFIG_RUNTIME_CONFIG_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "mb/browser/environment_paths.h"
#include "mb/config/config.h"

namespace mb::config {

enum class RuntimeConfigIssueCode {
  kConfigFile,
  kEnvironmentPath,
  kUnknownExplicitEnvironment,
  kStaleRememberedEnvironment,
  kInternalSelection,
};

struct RuntimeConfigIssue {
  RuntimeConfigIssueCode code = RuntimeConfigIssueCode::kConfigFile;
  std::string filename;
  std::string key;
  std::string message;
  std::size_t line = 0;
  int system_errno = 0;
};

// A value snapshot assembled before browser startup. Callers treat it as
// immutable after construction; this type intentionally owns no descriptors or
// mutable integration state.
struct RuntimeConfig {
  Config config;
  std::string config_filename;
  EnvironmentDirectories resolved_environment_directories;
  std::string selected_environment;
  std::string selected_root;
};

struct RuntimeConfigResult {
  std::optional<RuntimeConfig> snapshot;
  std::vector<RuntimeConfigIssue> errors;
  std::vector<RuntimeConfigIssue> warnings;

  [[nodiscard]] bool ok() const {
    return snapshot.has_value() && errors.empty();
  }
};

// Loads a configuration file, audits every configured environment path without
// creating directories, and selects an environment. |home| is explicit; this
// API never reads environment variables or persists remembered selection.
[[nodiscard]] RuntimeConfigResult LoadRuntimeConfig(
    const std::string& config_filename,
    const std::string& home,
    const std::optional<std::string>& explicit_environment = std::nullopt,
    const std::optional<std::string>& remembered_environment = std::nullopt);

}  // namespace mb::config

#endif  // MB_CONFIG_RUNTIME_CONFIG_H_
