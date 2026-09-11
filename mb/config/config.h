#ifndef MB_CONFIG_CONFIG_H_
#define MB_CONFIG_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mb::config {

// This is deliberately a pure text-to-data conversion. It does not read files,
// expand paths, create directories, or start processes.
enum class TabsPosition { kLeft };
enum class Theme { kSystem, kLight, kDark };

struct UiConfig {
  TabsPosition tabs_position = TabsPosition::kLeft;
  int sidebar_width = 280;
  bool sidebar_collapsed = false;
  bool show_tab_close_buttons = true;
  bool top_bar_visible = true;
  Theme theme = Theme::kSystem;
};

struct KeybindingsConfig {
  // An empty string disables the action.
  std::string toggle_top_bar = "Alt+K";
  std::string toggle_tab_bar = "Alt+H";
};

struct AppConfig {
  std::string default_environment = "personal";
  bool restore_last_environment = false;
};

struct EnvironmentConfig {
  std::string name;
  // Retained exactly as parsed. It is intentionally not expanded or resolved.
  std::string data_directory;
  std::optional<std::string> accent_color;
  std::vector<std::string> startup_urls;
};

struct Config {
  int schema_version = 1;
  UiConfig ui;
  KeybindingsConfig keybindings;
  AppConfig app;
  std::vector<EnvironmentConfig> environments;
};

struct Diagnostic {
  std::string filename;
  std::string key;
  std::string message;
  // One-based TOML source line, or zero when no source location exists.
  std::size_t line = 0;
};

struct ParseConfigResult {
  std::optional<Config> config;
  std::vector<Diagnostic> errors;

  [[nodiscard]] bool ok() const { return config.has_value() && errors.empty(); }
};

// Parses schema version 1 configuration from |text|. This API uses explicit
// errors and is built with TOML_EXCEPTIONS=0.
[[nodiscard]] ParseConfigResult ParseConfig(std::string_view text,
                                            std::string_view filename);

}  // namespace mb::config

#endif  // MB_CONFIG_CONFIG_H_
