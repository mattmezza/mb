#include "mb/config/config.h"

#define TOML_EXCEPTIONS 0
// Valid schema documents need only shallow tables/arrays. Bound both value
// recursion and dotted-key chains before the parser allocates deep node trees.
#define TOML_MAX_NESTED_VALUES 16
#define TOML_MAX_KEY_COMPONENTS 16
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
// toml++ has not adopted Chromium's span migration. Limit this diagnostic
// exception to the vendored header; product code below retains buffer checks.
// Parser byte/key/value bounds and compiler/runtime hardening remain enabled.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
#include "mb/third_party/tomlplusplus/toml.hpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace mb::config {
namespace {

constexpr std::size_t kMaxConfigBytes = 1024 * 1024;
constexpr std::size_t kMaxEnvironments = 256;
constexpr std::size_t kMaxStartupUrls = 100;

void AddError(ParseConfigResult& result,
              std::string_view filename,
              std::string_view key,
              std::string_view message,
              std::size_t line = 0) {
  result.errors.push_back(
      {std::string(filename), std::string(key), std::string(message), line});
}

std::size_t LineOf(const toml::node& node) {
  return node.source().begin.line;
}

bool HasControl(std::string_view value) {
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto character = static_cast<unsigned char>(value[i]);
    if (character < 0x20 || character == 0x7f)
      return true;
    // TOML has already verified UTF-8. C1 controls encode as C2 80..9F.
    if (character == 0xc2 && i + 1 < value.size()) {
      const auto next = static_cast<unsigned char>(value[i + 1]);
      if (next >= 0x80 && next <= 0x9f)
        return true;
    }
  }
  return false;
}

bool IsPortableEnvironmentName(std::string_view name) {
  if (name.empty() || name.size() > 64)
    return false;
  const auto is_alphanumeric = [](unsigned char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9');
  };
  if (!is_alphanumeric(static_cast<unsigned char>(name.front())))
    return false;
  return std::all_of(name.begin() + 1, name.end(), [&](char character) {
    return is_alphanumeric(static_cast<unsigned char>(character)) ||
           character == '_' || character == '-';
  });
}

bool IsHexColor(std::string_view color) {
  if (color.size() != 7 || color.front() != '#')
    return false;
  return std::all_of(color.begin() + 1, color.end(), [](char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
  });
}

bool IsShortcut(std::string_view shortcut) {
  if (shortcut.empty())
    return true;
  bool control = false;
  bool alt = false;
  bool shift = false;
  bool key = false;
  while (!shortcut.empty()) {
    const std::size_t separator = shortcut.find('+');
    const std::string_view token = shortcut.substr(0, separator);
    if (token == "Ctrl" && !control && !key)
      control = true;
    else if (token == "Alt" && !alt && !key)
      alt = true;
    else if (token == "Shift" && !shift && !key)
      shift = true;
    else if (!key && token.size() == 1 &&
             ((token[0] >= 'A' && token[0] <= 'Z') ||
              (token[0] >= '0' && token[0] <= '9')))
      key = true;
    else
      return false;
    if (separator == std::string_view::npos)
      break;
    shortcut.remove_prefix(separator + 1);
    if (shortcut.empty())
      return false;
  }
  return key && (control || alt || shift);
}

bool IsStartupUrl(std::string_view url) {
  if (url.empty() || HasControl(url))
    return false;
  if (url.find(' ') != std::string_view::npos) {
    return false;
  }

  const std::size_t separator = url.find(':');
  if (separator == std::string_view::npos)
    return false;
  const std::string_view scheme = url.substr(0, separator);
  const std::string_view remainder = url.substr(separator + 1);
  if (scheme == "about")
    return !remainder.empty() && remainder.find("//") != 0 &&
           remainder.find('@') == std::string_view::npos;
  if (scheme == "chrome") {
    if (remainder.size() < 3 || remainder.substr(0, 2) != "//")
      return false;
    const std::string_view authority = remainder.substr(2);
    const std::size_t authority_end = authority.find_first_of("/?#");
    const std::string_view host = authority.substr(0, authority_end);
    return !host.empty() && host.find('@') == std::string_view::npos;
  }
  if (scheme != "http" && scheme != "https")
    return false;
  if (remainder.size() < 3 || remainder.substr(0, 2) != "//")
    return false;
  const std::string_view authority = remainder.substr(2);
  const std::size_t authority_end = authority.find_first_of("/?#");
  const std::string_view host = authority.substr(0, authority_end);
  return !host.empty() && host.find('@') == std::string_view::npos;
}

bool IsOneOf(std::string_view key,
             std::initializer_list<std::string_view> allowed) {
  return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
}

void CheckUnknownKeys(ParseConfigResult& result,
                      std::string_view filename,
                      const toml::table& table,
                      std::string_view prefix,
                      std::initializer_list<std::string_view> allowed) {
  for (const auto& entry : table) {
    if (!IsOneOf(entry.first.str(), allowed)) {
      std::string full_key(prefix);
      if (!full_key.empty())
        full_key.push_back('.');
      full_key.append(entry.first.str());
      AddError(result, filename, full_key, "unknown key", LineOf(entry.second));
    }
  }
}

const toml::table* OptionalTable(ParseConfigResult& result,
                                 std::string_view filename,
                                 const toml::table& parent,
                                 std::string_view key) {
  const toml::node* node = parent.get(key);
  if (!node)
    return nullptr;
  const toml::table* table = node->as_table();
  if (!table)
    AddError(result, filename, key, "must be a table", LineOf(*node));
  return table;
}

const toml::table* RequiredTable(ParseConfigResult& result,
                                 std::string_view filename,
                                 const toml::table& parent,
                                 std::string_view key) {
  const toml::node* node = parent.get(key);
  if (!node) {
    AddError(result, filename, key, "is required");
    return nullptr;
  }
  const toml::table* table = node->as_table();
  if (!table)
    AddError(result, filename, key, "must be a table", LineOf(*node));
  return table;
}

const std::string* OptionalString(ParseConfigResult& result,
                                  std::string_view filename,
                                  const toml::table& table,
                                  std::string_view key,
                                  std::string_view full_key) {
  const toml::node* node = table.get(key);
  if (!node)
    return nullptr;
  const auto* value = node->as_string();
  if (!value) {
    AddError(result, filename, full_key, "must be a string", LineOf(*node));
    return nullptr;
  }
  return &value->get();
}

const std::int64_t* OptionalInteger(ParseConfigResult& result,
                                    std::string_view filename,
                                    const toml::table& table,
                                    std::string_view key,
                                    std::string_view full_key) {
  const toml::node* node = table.get(key);
  if (!node)
    return nullptr;
  const auto* value = node->as_integer();
  if (!value) {
    AddError(result, filename, full_key, "must be an integer", LineOf(*node));
    return nullptr;
  }
  return &value->get();
}

const bool* OptionalBoolean(ParseConfigResult& result,
                            std::string_view filename,
                            const toml::table& table,
                            std::string_view key,
                            std::string_view full_key) {
  const toml::node* node = table.get(key);
  if (!node)
    return nullptr;
  const auto* value = node->as_boolean();
  if (!value) {
    AddError(result, filename, full_key, "must be a boolean", LineOf(*node));
    return nullptr;
  }
  return &value->get();
}

void ParseUi(ParseConfigResult& result,
             std::string_view filename,
             const toml::table& table,
             UiConfig& ui) {
  CheckUnknownKeys(result, filename, table, "ui",
                   {"tabs_position", "sidebar_width", "sidebar_collapsed",
                    "show_tab_close_buttons", "top_bar_visible", "theme"});
  if (const std::string* value =
          OptionalString(result, filename, table, "tabs_position", "ui.tabs_position")) {
    if (*value != "left")
      AddError(result, filename, "ui.tabs_position", "must be \"left\"",
               LineOf(*table.get("tabs_position")));
  }
  if (const std::int64_t* value =
          OptionalInteger(result, filename, table, "sidebar_width", "ui.sidebar_width")) {
    if (*value < 126 || *value > 400) {
      AddError(result, filename, "ui.sidebar_width", "must be between 126 and 400",
               LineOf(*table.get("sidebar_width")));
    } else {
      ui.sidebar_width = static_cast<int>(*value);
    }
  }
  if (const bool* value = OptionalBoolean(result, filename, table, "sidebar_collapsed",
                                           "ui.sidebar_collapsed")) {
    ui.sidebar_collapsed = *value;
  }
  if (const bool* value = OptionalBoolean(result, filename, table,
                                           "show_tab_close_buttons",
                                           "ui.show_tab_close_buttons")) {
    ui.show_tab_close_buttons = *value;
  }
  if (const bool* value = OptionalBoolean(result, filename, table,
                                           "top_bar_visible",
                                           "ui.top_bar_visible")) {
    ui.top_bar_visible = *value;
  }
  if (const std::string* value = OptionalString(result, filename, table, "theme", "ui.theme")) {
    if (*value == "system")
      ui.theme = Theme::kSystem;
    else if (*value == "light")
      ui.theme = Theme::kLight;
    else if (*value == "dark")
      ui.theme = Theme::kDark;
    else
      AddError(result, filename, "ui.theme", "must be \"system\", \"light\", or \"dark\"",
               LineOf(*table.get("theme")));
  }
}

void ParseKeybindings(ParseConfigResult& result,
                      std::string_view filename,
                      const toml::table& table,
                      KeybindingsConfig& keybindings) {
  CheckUnknownKeys(result, filename, table, "keybindings",
                   {"toggle_top_bar", "toggle_tab_bar"});
  const auto parse = [&](std::string_view key, std::string& destination) {
    const std::string full_key = "keybindings." + std::string(key);
    if (const std::string* value =
            OptionalString(result, filename, table, key, full_key)) {
      if (!IsShortcut(*value)) {
        AddError(result, filename, full_key,
                 "must be empty (disabled) or use Ctrl/Alt/Shift plus one A-Z or 0-9 key",
                 LineOf(*table.get(key)));
      } else {
        destination = *value;
      }
    }
  };
  parse("toggle_top_bar", keybindings.toggle_top_bar);
  parse("toggle_tab_bar", keybindings.toggle_tab_bar);
  if (!keybindings.toggle_top_bar.empty() &&
      keybindings.toggle_top_bar == keybindings.toggle_tab_bar) {
    AddError(result, filename, "keybindings.toggle_tab_bar",
             "must not conflict with keybindings.toggle_top_bar",
             LineOf(*table.get("toggle_tab_bar")));
  }
}

void ParseApp(ParseConfigResult& result,
              std::string_view filename,
              const toml::table& table,
              AppConfig& app) {
  CheckUnknownKeys(result, filename, table, "app",
                   {"default_environment", "restore_last_environment"});
  if (const std::string* value = OptionalString(result, filename, table,
                                                "default_environment",
                                                "app.default_environment")) {
    app.default_environment = *value;
  }
  if (const bool* value = OptionalBoolean(result, filename, table,
                                           "restore_last_environment",
                                           "app.restore_last_environment")) {
    app.restore_last_environment = *value;
  }
}

void ParseEnvironment(ParseConfigResult& result,
                      std::string_view filename,
                      std::string_view name,
                      const toml::table& table,
                      EnvironmentConfig& environment) {
  const std::string prefix = "environments." + std::string(name);
  CheckUnknownKeys(result, filename, table, prefix,
                   {"data_directory", "accent_color", "startup_urls"});
  environment.name = std::string(name);

  const toml::node* directory_node = table.get("data_directory");
  if (!directory_node) {
    AddError(result, filename, prefix + ".data_directory", "is required");
  } else if (const auto* directory = directory_node->as_string()) {
    const std::string& value = directory->get();
    if (value.empty() || HasControl(value)) {
      AddError(result, filename, prefix + ".data_directory",
               "must be nonempty and contain no control characters", LineOf(*directory_node));
    } else {
      environment.data_directory = value;
    }
  } else {
    AddError(result, filename, prefix + ".data_directory", "must be a string",
             LineOf(*directory_node));
  }

  if (const std::string* color = OptionalString(result, filename, table, "accent_color",
                                                 prefix + ".accent_color")) {
    if (!IsHexColor(*color)) {
      AddError(result, filename, prefix + ".accent_color", "must match #RRGGBB",
               LineOf(*table.get("accent_color")));
    } else {
      environment.accent_color = *color;
    }
  }

  const toml::node* urls_node = table.get("startup_urls");
  if (!urls_node)
    return;
  const toml::array* urls = urls_node->as_array();
  if (!urls) {
    AddError(result, filename, prefix + ".startup_urls", "must be an array of strings",
             LineOf(*urls_node));
    return;
  }
  if (urls->size() > kMaxStartupUrls) {
    AddError(result, filename, prefix + ".startup_urls", "may contain at most 100 URLs",
             LineOf(*urls_node));
  }
  for (std::size_t index = 0; index < urls->size(); ++index) {
    const toml::node& url_node = (*urls)[index];
    const std::string index_key = prefix + ".startup_urls[" + std::to_string(index) + "]";
    const auto* url = url_node.as_string();
    if (!url) {
      AddError(result, filename, index_key, "must be a string", LineOf(url_node));
      continue;
    }
    if (!IsStartupUrl(url->get())) {
      AddError(result, filename, index_key,
               "must be a supported http, https, about, or chrome URL without userinfo",
               LineOf(url_node));
      continue;
    }
    if (index < kMaxStartupUrls)
      environment.startup_urls.push_back(url->get());
  }
}

}  // namespace

ParseConfigResult ParseConfig(std::string_view text, std::string_view filename) {
  ParseConfigResult result;
  if (text.size() > kMaxConfigBytes) {
    AddError(result, filename, "<document>", "configuration exceeds the 1 MiB limit");
    return result;
  }

  toml::parse_result parsed = toml::parse(text, std::string(filename));
  if (parsed.failed()) {
    const toml::parse_error& error = parsed.error();
    AddError(result, filename, "<toml>", error.description(), error.source().begin.line);
    return result;
  }

  const toml::table& root = parsed.table();
  CheckUnknownKeys(result, filename, root, "",
                   {"schema_version", "ui", "keybindings", "app",
                    "environments"});
  Config config;

  const toml::node* version_node = root.get("schema_version");
  if (!version_node) {
    AddError(result, filename, "schema_version", "is required");
  } else if (const auto* version = version_node->as_integer()) {
    if (version->get() != 1) {
      AddError(result, filename, "schema_version", "must be 1", LineOf(*version_node));
    }
  } else {
    AddError(result, filename, "schema_version", "must be an integer", LineOf(*version_node));
  }

  if (const toml::table* ui = OptionalTable(result, filename, root, "ui"))
    ParseUi(result, filename, *ui, config.ui);
  if (const toml::table* keybindings =
          OptionalTable(result, filename, root, "keybindings")) {
    ParseKeybindings(result, filename, *keybindings, config.keybindings);
  }
  if (const toml::table* app = OptionalTable(result, filename, root, "app"))
    ParseApp(result, filename, *app, config.app);

  const toml::table* environments = RequiredTable(result, filename, root, "environments");
  if (environments) {
    if (environments->empty()) {
      AddError(result, filename, "environments", "must contain at least one environment",
               LineOf(*root.get("environments")));
    }
    if (environments->size() > kMaxEnvironments) {
      AddError(result, filename, "environments", "may contain at most 256 environments",
               LineOf(*root.get("environments")));
    }
    std::size_t index = 0;
    for (const auto& entry : *environments) {
      const std::string_view name = entry.first.str();
      const std::string prefix = "environments." + std::string(name);
      if (!IsPortableEnvironmentName(name)) {
        AddError(result, filename, prefix,
                 "name must match [A-Za-z0-9][A-Za-z0-9_-]{0,63}", LineOf(entry.second));
      }
      const toml::table* table = entry.second.as_table();
      if (!table) {
        AddError(result, filename, prefix, "must be a table", LineOf(entry.second));
        continue;
      }
      EnvironmentConfig environment;
      ParseEnvironment(result, filename, name, *table, environment);
      if (index < kMaxEnvironments)
        config.environments.push_back(std::move(environment));
      ++index;
    }
  }

  const bool has_default = std::any_of(
      config.environments.begin(), config.environments.end(), [&](const EnvironmentConfig& environment) {
        return environment.name == config.app.default_environment;
      });
  if (!has_default) {
    AddError(result, filename, "app.default_environment",
             "must name a configured environment");
  }

  if (result.errors.empty())
    result.config = std::move(config);
  return result;
}

}  // namespace mb::config
