// Companion configuration command. No browser process or daemon is started.
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "branding.h"
#include "mb/app/startup_arguments.h"
#include "mb/browser/environment_paths.h"
#include "mb/config/config_file.h"

namespace {

std::string SafeDiagnostic(const std::string& text) {
  constexpr char digits[] = "0123456789abcdef";
  std::string safe;
  auto escape = [&](unsigned char ch) {
    safe += "\\x";
    safe += digits[ch >> 4];
    safe += digits[ch & 15];
  };
  for (size_t i = 0; i < text.size();) {
    const auto ch = static_cast<unsigned char>(text[i]);
    if (ch < 0x20 || ch == 0x7f) {
      escape(ch);
      ++i;
    } else if (ch < 0x80) {
      safe += ch;
      ++i;
    } else {
      size_t count = 0;
      unsigned codepoint = 0;
      unsigned minimum = 0;
      if (ch >= 0xc2 && ch <= 0xdf) {
        count = 2; codepoint = ch & 0x1f; minimum = 0x80;
      } else if (ch >= 0xe0 && ch <= 0xef) {
        count = 3; codepoint = ch & 0xf; minimum = 0x800;
      } else if (ch >= 0xf0 && ch <= 0xf4) {
        count = 4; codepoint = ch & 7; minimum = 0x10000;
      }
      bool valid = count && i + count <= text.size();
      for (size_t j = 1; valid && j < count; ++j) {
        const auto continuation = static_cast<unsigned char>(text[i + j]);
        valid = (continuation & 0xc0) == 0x80;
        codepoint = (codepoint << 6) | (continuation & 0x3f);
      }
      valid = valid && codepoint >= minimum && codepoint <= 0x10ffff &&
              !(codepoint >= 0xd800 && codepoint <= 0xdfff);
      if (!valid) {
        escape(ch);
        ++i;
      } else if ((codepoint >= 0x80 && codepoint <= 0x9f) ||
                 (codepoint >= 0x202a && codepoint <= 0x202e) ||
                 (codepoint >= 0x2066 && codepoint <= 0x2069)) {
        for (size_t j = 0; j < count; ++j)
          escape(static_cast<unsigned char>(text[i++]));
      } else {
        safe.append(text, i, count);
        i += count;
      }
    }
  }
  return safe;
}

int Error(const std::string& message) {
  std::cerr << SafeDiagnostic(message) << '\n';
  return 2;
}

std::string Variable(const char* key) {
  const char* value = std::getenv(key);
  return value ? value : "";
}

void Usage() {
  std::cout << "Usage: " << mb::branding::kExecutableName
            << "ctl [--config PATH] config validate\n       "
            << mb::branding::kExecutableName
            << "ctl [--config PATH] environment list\n       "
            << mb::branding::kExecutableName
            << "ctl [--config PATH] environment path NAME\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> raw;
  for (int i = 0; i < argc; ++i)
    raw.emplace_back(argv[i]);
  auto normalized = mb::NormalizeStartupArguments(raw);
  if (!normalized.value)
    return Error(normalized.error);
  const auto& arguments = *normalized.value;
  if (arguments.environment || arguments.has_user_data_dir)
    return Error("command line: environment selectors and Chromium flags are not control commands");
  std::vector<std::string> words;
  for (size_t i = 1; i < arguments.argv.size(); ++i) {
    const auto& word = arguments.argv[i];
    if (word.starts_with("--config="))
      continue;
    words.push_back(word);
  }
  if (words == std::vector<std::string>{"--help"}) {
    Usage();
    return 0;
  }
  const bool validate = words == std::vector<std::string>{"config", "validate"};
  const bool list = words == std::vector<std::string>{"environment", "list"};
  const bool path = words.size() == 3 && words[0] == "environment" && words[1] == "path";
  if (!validate && !list && !path) {
    Usage();
    return Error("command line: expected config validate, environment list, or environment path NAME");
  }
  const std::string home = Variable("HOME");
  if (home.empty() || home[0] != '/')
    return Error("HOME: must be an absolute path");
  std::string filename;
  if (arguments.config_path) {
    filename = *arguments.config_path;
  } else {
    std::string config_home = Variable("XDG_CONFIG_HOME");
    if (config_home.empty())
      config_home = home + "/.config";
    if (config_home[0] != '/')
      return Error("XDG_CONFIG_HOME: must be an absolute path when set");
    filename = config_home + "/" + mb::branding::kProfileDirectoryName + "/config.toml";
  }
  auto loaded = mb::config::LoadConfigFile(filename);
  if (!loaded.parsed.ok()) {
    for (const auto& diagnostic : loaded.parsed.errors) {
      const std::string location = diagnostic.line ? ":" + std::to_string(diagnostic.line) : "";
      Error(diagnostic.filename + location + ": " + diagnostic.key + ": " + diagnostic.message);
    }
    return 2;
  }
  mb::EnvironmentDirectories directories;
  for (const auto& environment : loaded.parsed.config->environments)
    directories.emplace(environment.name, environment.data_directory);
  mb::EnvironmentDirectories resolved;
  mb::EnvironmentPathError error;
  if (!mb::AuditEnvironmentPaths(loaded.filename, home, directories, &resolved, &error))
    return Error(loaded.filename + ": " + error.key + ": " + error.message);
  if (validate) {
    std::cout << "Configuration valid: " << SafeDiagnostic(loaded.filename) << '\n';
  } else if (list) {
    for (const auto& [name, directory] : resolved)
      std::cout << name << '\n';
  } else {
    const auto found = resolved.find(words[2]);
    if (found == resolved.end())
      return Error("environment: unknown name: " + words[2]);
    std::cout << found->second << '\n';
  }
  return 0;
}
