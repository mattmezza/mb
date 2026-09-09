// SPDX-License-Identifier: BSD-3-Clause
#include "mb/browser/browser_config_gate.h"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "chrome/common/chrome_result_codes.h"
#include "chrome/common/chrome_switches.h"
#include "mb/browser/browser_config_gate_internal.h"
#include "mb/browser/environment_paths.h"
#include "mb/browser/user_config_state.h"
#include "mb/generated/branding/branding.h"
#include "url/third_party/mozilla/url_parse.h"
#include "url/url_canon.h"
#include "url/url_canon_stdstring.h"

namespace mb {
namespace {

struct InstalledConfig {
  config::RuntimeConfig snapshot;
  // Keeps the securely prepared directory open, but does not replace the
  // native singleton or bind Chromium's later absolute-path lookups.
  PreparedEnvironmentRoot root;
};

std::unique_ptr<const InstalledConfig>& Storage() {
  static base::NoDestructor<std::unique_ptr<const InstalledConfig>> installed;
  return *installed;
}

// Escape filename/key and permitted diagnostic detail, including every
// non-printable/non-ASCII byte, bidi controls and invalid UTF-8.
std::string QuoteDiagnostic(std::string_view text) {
  constexpr std::string_view hex = "0123456789abcdef";
  constexpr size_t max_bytes = 4096;
  std::string result = "\"";
  for (const unsigned char ch : text.substr(0, max_bytes)) {
    if (ch >= 0x20 && ch < 0x7f && ch != '"' && ch != '\\') {
      result += ch;
    } else {
      result += "\\x";
      result += hex[ch >> 4];
      result += hex[ch & 15];
    }
  }
  if (text.size() > max_bytes) {
    result += "...";
  }
  return result + '"';
}

std::string FormatDiagnostic(std::string_view filename,
                             std::string_view key,
                             std::string_view reason,
                             size_t line,
                             int system_errno) {
  std::string result = "configuration " + QuoteDiagnostic(filename) + ": " +
                       QuoteDiagnostic(key);
  if (line) {
    result += " (line " + std::to_string(line) + ')';
  }
  result += ": " + QuoteDiagnostic(reason);
  if (system_errno) {
    result += " (errno " + std::to_string(system_errno) + ')';
  }
  return result;
}

std::optional<int> Fail(std::string_view filename,
                        std::string_view key,
                        std::string_view reason,
                        size_t line = 0,
                        int system_errno = 0) {
  std::cerr << FormatDiagnostic(filename, key, reason, line, system_errno)
            << '\n';
  return CHROME_RESULT_CODE_UNSUPPORTED_PARAM;
}

std::string_view RuntimeIssueReason(const config::RuntimeConfigIssue& issue) {
  switch (issue.code) {
    case config::RuntimeConfigIssueCode::kConfigFile:
      if (issue.key == "<toml>") {
        return "invalid TOML syntax";
      }
      // Schema diagnostics are fixed expected-type/range/unknown-key messages
      // in config.cc. File diagnostics contain fixed context and strerror.
      return issue.message;
    case config::RuntimeConfigIssueCode::kEnvironmentPath:
      // May identify a configured component; FormatDiagnostic bounds and
      // escapes it. These path diagnostics contain no startup URL values.
      return issue.message;
    case config::RuntimeConfigIssueCode::kUnknownExplicitEnvironment:
      return "explicit environment is not configured";
    case config::RuntimeConfigIssueCode::kStaleRememberedEnvironment:
      return issue.message;
    case config::RuntimeConfigIssueCode::kInternalSelection:
      return "configured environment could not be selected";
  }
  return "configuration validation failed";
}

}  // namespace

namespace internal {

std::string FormatRuntimeConfigIssue(const config::RuntimeConfigIssue& issue) {
  return FormatDiagnostic(issue.filename, issue.key, RuntimeIssueReason(issue),
                          issue.line, issue.system_errno);
}

bool NativeRootMatches(const config::RuntimeConfig& snapshot,
                       const std::string& home,
                       const base::FilePath& supplied) {
  if (!supplied.IsAbsolute()) {
    return false;
  }
  EnvironmentDirectories resolved;
  EnvironmentPathError error;
  return ResolveEnvironmentPaths(snapshot.config_filename, home,
                                 {{"native", supplied.value()}}, &resolved,
                                 &error) &&
         resolved.at("native") == snapshot.selected_root;
}

bool IsValidStartupUrl(std::string_view raw) {
  // GURL consults the global scheme registry and marks it used. This hook runs
  // before Content registers schemes, so use only stateless native parsers and
  // canonicalizers with an explicit scheme type.
  url::Component scheme;
  if (!url::ExtractScheme(raw, &scheme)) return false;
  const std::string name = base::ToLowerASCII(scheme.AsViewOn(raw));
  std::string canonical;
  url::StdStringCanonOutput output(&canonical);
  url::Parsed parsed;
  if (name == "about") {
    const bool valid = url::CanonicalizePathUrl(
        raw, url::ParsePathUrl(raw, true), &output, &parsed);
    output.Complete();
    return valid && parsed.path.is_nonempty();
  }
  if (name != "http" && name != "https" && name != "chrome") return false;
  const auto input = url::ParseStandardUrl(raw);
  if (input.username.is_valid() || input.password.is_valid()) return false;
  const auto type = name == "chrome" ? url::SCHEME_WITH_HOST
      : url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION;
  const bool valid = url::CanonicalizeStandardUrl(
      raw, input, type, nullptr, &output, &parsed);
  output.Complete();
  return valid && parsed.host.is_nonempty() && !parsed.username.is_valid() &&
         !parsed.password.is_valid();
}

}  // namespace internal

const config::RuntimeConfig* GetProcessRuntimeConfig() {
  return Storage() ? &Storage()->snapshot : nullptr;
}

std::optional<int> InitializeExplicitBrowserConfig() {
  base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch("type")) {
    const auto process_type = command_line.GetSwitchValueNative("type");
    if (process_type.empty() || !base::IsStringASCII(process_type)) {
      return Fail("<command-line>", "--type",
                  "process type must have a nonempty ASCII value");
    }
    return std::nullopt;
  }

  const bool has_explicit_config = command_line.HasSwitch("config");
  std::string filename = command_line.GetSwitchValueNative("config");
  auto environment = base::Environment::Create();
  const auto root_override = environment->GetVar("CHROME_USER_DATA_DIR");
  if (root_override && !root_override->empty()) {
    return Fail(filename, "CHROME_USER_DATA_DIR",
                "user-data directory environment override is not supported");
  }

  // Native --user-data-dir is an intentional opt-out from product default
  // discovery. Do not create XDG config, state, or an environment root.
  if (!has_explicit_config && !command_line.HasSwitch("environment") &&
      command_line.HasSwitch(switches::kUserDataDir)) {
    return std::nullopt;
  }
  if (Storage()) {
    return Fail(filename, "--config",
                "process configuration was already initialized");
  }
  if (has_explicit_config &&
      (filename.empty() ||
       base::TrimWhitespaceASCII(filename, base::TRIM_ALL) != filename)) {
    return Fail(
        filename, "--config",
        "configuration filename must be nonempty without boundary whitespace");
  }

  std::optional<std::string> selected;
  if (command_line.HasSwitch("environment")) {
    selected = command_line.GetSwitchValueNative("environment");
    if (selected->empty() ||
        base::TrimWhitespaceASCII(*selected, base::TRIM_ALL) != *selected) {
      return Fail(filename, "--environment",
                  "environment must be nonempty without boundary whitespace");
    }
  }
  const auto home = environment->GetVar("HOME");
  if (!home || home->empty() || !base::FilePath(*home).IsAbsolute()) {
    return Fail(filename, "HOME", "an absolute home directory is required");
  }

  // State is deliberately local to the discovered default config lifecycle.
  // Explicit --config stays fully explicit and never creates or consumes XDG
  // state as a side effect.
  std::optional<UserConfigPaths> local_paths;
  std::optional<std::string> remembered;
  if (!has_explicit_config) {
    UserConfigPaths paths;
    UserConfigStateError state_error;
    if (!ResolveUserConfigPaths(*home, environment->GetVar("XDG_CONFIG_HOME"),
                                environment->GetVar("XDG_STATE_HOME"),
                                environment->GetVar("XDG_DATA_HOME"),
                                branding::kProfileDirectoryName, &paths,
                                &state_error)) {
      return Fail("<xdg>", state_error.key, state_error.message, 0,
                  state_error.system_errno);
    }
    bool created = false;
    if (!EnsureDefaultConfig(paths, *home, &created, &state_error)) {
      return Fail(paths.config_file, state_error.key, state_error.message, 0,
                  state_error.system_errno);
    }
    filename = paths.config_file;
    (void)created;  // The template is parsed and validated below.
    if (!ReadRememberedEnvironment(paths, *home, &remembered, &state_error)) {
      std::cerr << FormatDiagnostic(paths.state_file, state_error.key,
                                    state_error.message, 0,
                                    state_error.system_errno)
                << '\n';
      remembered.reset();
    }
    local_paths = std::move(paths);
  }

  auto loaded =
      config::LoadRuntimeConfig(filename, *home, selected, remembered);
  if (!loaded.ok()) {
    for (const auto& issue : loaded.errors) {
      std::cerr << internal::FormatRuntimeConfigIssue(issue) << '\n';
    }
    return CHROME_RESULT_CODE_UNSUPPORTED_PARAM;
  }
  for (const auto& warning : loaded.warnings) {
    std::cerr << internal::FormatRuntimeConfigIssue(warning) << '\n';
  }
  auto& snapshot = *loaded.snapshot;

  // All roots were audited nonmutating by LoadRuntimeConfig. Check full URL
  // validity for every environment, including unselected ones, before mkdir.
  for (const auto& entry : snapshot.config.environments) {
    for (size_t i = 0; i < entry.startup_urls.size(); ++i) {
      if (!internal::IsValidStartupUrl(entry.startup_urls[i])) {
        return Fail(snapshot.config_filename,
                    "environments." + entry.name + ".startup_urls[" +
                        std::to_string(i) + "]",
                    "startup URL failed Chromium URL validation");
      }
    }
  }

  if (command_line.HasSwitch(switches::kUserDataDir)) {
    const auto supplied =
        command_line.GetSwitchValuePath(switches::kUserDataDir);
    if (!supplied.IsAbsolute()) {
      return Fail(snapshot.config_filename, "--user-data-dir",
                  "native user-data directory must be absolute");
    }
    if (!internal::NativeRootMatches(snapshot, *home, supplied)) {
      return Fail(
          snapshot.config_filename, "--user-data-dir",
          "native user-data directory must resolve to the selected root");
    }
  }

  PreparedEnvironmentRoot prepared;
  EnvironmentPathError error;
  if (!PrepareEnvironmentRoot(snapshot.config_filename, *home,
                              snapshot.resolved_environment_directories,
                              snapshot.selected_environment, &prepared,
                              &error)) {
    return Fail(snapshot.config_filename, error.key, error.message, 0,
                error.system_errno);
  }

  command_line.RemoveSwitch(switches::kUserDataDir);
  command_line.AppendSwitchPath(switches::kUserDataDir,
                                base::FilePath(snapshot.selected_root));
  Storage() = std::make_unique<const InstalledConfig>(
      InstalledConfig{std::move(snapshot), std::move(prepared)});

  // Incognito uses the selected root but never records it as the next normal
  // browser selection. State-write failures remain nonfatal after successful
  // startup and are surfaced as bounded diagnostics.
  if (local_paths && !command_line.HasSwitch(switches::kIncognito) &&
      Storage()->snapshot.config.app.restore_last_environment) {
    UserConfigStateError state_error;
    if (!WriteRememberedEnvironment(*local_paths, *home,
                                    Storage()->snapshot.selected_environment,
                                    &state_error)) {
      std::cerr << FormatDiagnostic(local_paths->state_file, state_error.key,
                                    state_error.message, 0,
                                    state_error.system_errno)
                << '\n';
    }
  }
  return std::nullopt;
}
}  // namespace mb
