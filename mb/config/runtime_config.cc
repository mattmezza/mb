#include "mb/config/runtime_config.h"

#include <utility>

#include "mb/config/config_file.h"

namespace mb::config {
namespace {

void AddConfigErrors(RuntimeConfigResult& result, const ConfigFileResult& file) {
  for (const Diagnostic& error : file.parsed.errors) {
    result.errors.push_back({RuntimeConfigIssueCode::kConfigFile, error.filename,
                             error.key, error.message, error.line, 0});
  }
}

void AddPathError(RuntimeConfigResult& result,
                  const std::string& filename,
                  const EnvironmentPathError& error) {
  result.errors.push_back({RuntimeConfigIssueCode::kEnvironmentPath, filename,
                           error.key, error.message, 0, error.system_errno});
}

EnvironmentDirectories DirectoriesFrom(const Config& config) {
  EnvironmentDirectories directories;
  for (const EnvironmentConfig& environment : config.environments)
    directories.emplace(environment.name, environment.data_directory);
  return directories;
}

}  // namespace

RuntimeConfigResult LoadRuntimeConfig(
    const std::string& config_filename,
    const std::string& home,
    const std::optional<std::string>& explicit_environment,
    const std::optional<std::string>& remembered_environment) {
  RuntimeConfigResult result;
  ConfigFileResult file = LoadConfigFile(config_filename);
  if (!file.parsed.ok()) {
    AddConfigErrors(result, file);
    return result;
  }

  Config config = std::move(*file.parsed.config);
  const EnvironmentDirectories directories = DirectoriesFrom(config);
  EnvironmentDirectories resolved;
  EnvironmentPathError path_error;
  if (!AuditEnvironmentPaths(file.filename, home, directories, &resolved,
                             &path_error)) {
    AddPathError(result, file.filename, path_error);
    return result;
  }

  std::string selected = config.app.default_environment;
  if (explicit_environment) {
    if (!resolved.contains(*explicit_environment)) {
      result.errors.push_back({RuntimeConfigIssueCode::kUnknownExplicitEnvironment,
                               file.filename, "--environment",
                               "Explicit environment is not configured: " +
                                   *explicit_environment,
                               0, 0});
      return result;
    }
    selected = *explicit_environment;
  } else if (config.app.restore_last_environment && remembered_environment) {
    if (resolved.contains(*remembered_environment)) {
      selected = *remembered_environment;
    } else {
      result.warnings.push_back({RuntimeConfigIssueCode::kStaleRememberedEnvironment,
                                 file.filename, "app.restore_last_environment",
                                 "Remembered environment is no longer configured: " +
                                     *remembered_environment +
                                     "; falling back to configured default: " +
                                     config.app.default_environment,
                                 0, 0});
    }
  }

  const auto selected_root = resolved.find(selected);
  if (selected_root == resolved.end()) {
    result.errors.push_back({RuntimeConfigIssueCode::kInternalSelection,
                             file.filename, "app.default_environment",
                             "Configured default environment was not resolved", 0, 0});
    return result;
  }

  const std::string selected_root_path = selected_root->second;
  result.snapshot = RuntimeConfig{std::move(config), std::move(file.filename),
                                  std::move(resolved), std::move(selected),
                                  selected_root_path};
  return result;
}

}  // namespace mb::config
