#include "mb/app/startup_arguments.h"

#include <string_view>
#include <utility>

namespace mb {
namespace {

StartupArgumentsResult Error(std::string message) {
  return {std::nullopt, std::move(message)};
}

bool IsSwitch(std::string_view token, std::string_view name) {
  return token == name ||
         (token.starts_with(name) && token.size() > name.size() &&
          token[name.size()] == '=');
}

std::string_view SwitchToken(std::string_view token) {
  // Match Chromium's POSIX switch recognition, including its ASCII trimming
  // and single-dash aliases. Do not rewrite unrelated original argv tokens.
  constexpr std::string_view whitespace = " \t\n\v\f\r";
  const auto first = token.find_first_not_of(whitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  return token.substr(first, token.find_last_not_of(whitespace) - first + 1);
}

}  // namespace

StartupArgumentsResult NormalizeStartupArguments(
    const std::vector<std::string>& argv) {
  if (argv.empty() || argv[0].empty()) {
    return Error("command line: missing executable name");
  }
  for (const auto& token : argv) {
    if (token.find('\0') != std::string::npos) {
      return Error("command line: embedded NUL is not allowed");
    }
  }
  StartupArguments result;
  result.argv.reserve(argv.size());
  result.argv.push_back(argv[0]);
  bool switches = true;
  for (size_t index = 1; index < argv.size(); ++index) {
    std::string_view token = SwitchToken(argv[index]);
    if (token == "--") {
      switches = false;
    }
    if (!switches) {
      result.argv.push_back(argv[index]);
      continue;
    }
    std::optional<std::string>* destination = nullptr;
    std::string_view name;
    // Chromium accepts both prefixes on POSIX. Canonicalize product aliases
    // before duplicate detection so -environment cannot bypass that policy.
    if (token.starts_with("--")) {
      token.remove_prefix(1);
    }
    if (IsSwitch(token, "-config")) {
      destination = &result.config_path;
      name = "-config";
    } else if (IsSwitch(token, "-environment")) {
      destination = &result.environment;
      name = "-environment";
    } else {
      if (IsSwitch(token, "-user-data-dir")) {
        result.has_user_data_dir = true;
      }
      result.argv.push_back(argv[index]);
      continue;
    }
    if (destination->has_value()) {
      return Error("command line: -" + std::string(name) + " specified more than once");
    }
    std::string value;
    if (token.size() > name.size()) {
      value = token.substr(name.size() + 1);
    } else {
      if (index + 1 == argv.size() || SwitchToken(argv[index + 1]).starts_with('-')) {
        return Error("command line: -" + std::string(name) + " requires a value");
      }
      value = argv[++index];
    }
    if (value.empty()) {
      return Error("command line: -" + std::string(name) + " must not be empty");
    }
    *destination = value;
    result.argv.push_back("-" + std::string(name) + "=" + value);
  }
  return {std::move(result), {}};
}

}  // namespace mb
