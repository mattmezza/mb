// SPDX-License-Identifier: BSD-3-Clause
#include "mb/app/chrome_startup_arguments.h"

#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/strings/string_util.h"

namespace mb {
namespace {

bool HasBoundaryWhitespace(std::string_view value) {
  return base::TrimWhitespaceASCII(value, base::TRIM_ALL) != value;
}

}  // namespace

StartupArgumentsResult NormalizeChromeStartupArguments(
    const std::vector<std::string>& raw_argv,
    const std::vector<std::string>& extra_argv) {
  if (raw_argv.empty() || raw_argv[0].empty()) {
    return {std::nullopt, "command line: missing executable name"};
  }

  // Use Chromium's POSIX parser only to classify its process switch. The
  // browser normalizer below receives the original tokens, never argv() from
  // this parser, which trims/reorders switches and collapses their map entries.
  const base::CommandLine raw_command_line(raw_argv);
  if (raw_command_line.HasSwitch("type")) {
    // Content dispatches an empty type as a browser, while several Chrome
    // startup gates use HasSwitch. Reject that ambiguous invocation here.
    const auto process_type = raw_command_line.GetSwitchValueNative("type");
    if (process_type.empty() || !base::IsStringASCII(process_type)) {
      return {std::nullopt,
              "command line: --type requires a non-empty ASCII value"};
    }
    StartupArguments child;
    child.argv = raw_argv;
    return {std::move(child), {}};
  }

  auto normalized = NormalizeStartupArguments(raw_argv);
  if (!normalized.value) {
    return normalized;
  }
  for (const auto* value : {&normalized.value->config_path,
                            &normalized.value->environment}) {
    if (*value && HasBoundaryWhitespace(**value)) {
      return {std::nullopt,
              "command line: --config and --environment values must not have "
              "leading or trailing ASCII whitespace"};
    }
  }
  // The pure normalizer follows Chromium's trimming of complete tokens. Check
  // equals-form values in the original tokens as well, before that trim could
  // conceal trailing whitespace. Internal spaces are valid.
  for (size_t i = 1; i < raw_argv.size(); ++i) {
    const auto token =
        base::TrimWhitespaceASCII(raw_argv[i], base::TRIM_LEADING);
    if (base::TrimWhitespaceASCII(token, base::TRIM_ALL) == "--") {
      break;
    }
    for (std::string_view prefix : {"--config=", "-config=", "--environment=",
                                    "-environment="}) {
      if (token.starts_with(prefix) &&
          HasBoundaryWhitespace(token.substr(prefix.size()))) {
        return {std::nullopt,
                "command line: --config and --environment values must not "
                "have leading or trailing ASCII whitespace"};
      }
    }
  }
  if (extra_argv.empty()) {
    return {std::nullopt, "extra flags: missing program sentinel"};
  }

  // Validate before constructing a CommandLine from extra flags: otherwise
  // repeated product selectors would already have lost their distinct values.
  auto validation_argv = extra_argv;
  validation_argv[0] = "CHROME_EXTRA_FLAGS";
  auto extra = NormalizeStartupArguments(validation_argv);
  if (!extra.value) {
    return {std::nullopt, "extra flags: " + extra.error};
  }
  if (extra.value->config_path || extra.value->environment ||
      extra.value->has_user_data_dir ||
      base::CommandLine(extra_argv).HasSwitch("type")) {
    return {std::nullopt,
            "extra flags: --config, --environment, --user-data-dir and --type "
            "must be supplied on the explicit command line"};
  }
  return normalized;
}

}  // namespace mb
