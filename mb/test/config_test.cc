#include "mb/config/config.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace mb::config {
namespace {

constexpr char kValidConfig[] = R"toml(
schema_version = 1

[environments.personal]
data_directory = "~/literal-$HOME"
)toml";

TEST(ConfigTest, ParsesValidDefaults) {
  const ParseConfigResult result = ParseConfig(kValidConfig, "config.toml");
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result.config.has_value());
  EXPECT_EQ(result.config->ui.sidebar_width, 280);
  EXPECT_FALSE(result.config->ui.sidebar_collapsed);
  EXPECT_TRUE(result.config->ui.show_tab_close_buttons);
  EXPECT_TRUE(result.config->ui.top_bar_visible);
  EXPECT_EQ(result.config->keybindings.toggle_top_bar, "Alt+K");
  EXPECT_EQ(result.config->keybindings.toggle_tab_bar, "Alt+H");
  EXPECT_EQ(result.config->ui.theme, Theme::kSystem);
  EXPECT_EQ(result.config->app.default_environment, "personal");
  ASSERT_EQ(result.config->environments.size(), 1u);
  EXPECT_EQ(result.config->environments[0].data_directory, "~/literal-$HOME");
}

TEST(ConfigTest, ParsesAndValidatesUiVisibilityAndKeybindings) {
  const auto valid = ParseConfig(R"toml(
schema_version = 1
[ui]
top_bar_visible = false
[keybindings]
toggle_top_bar = "Ctrl+L"
toggle_tab_bar = ""
[environments.personal]
data_directory = "/tmp/personal"
)toml", "keys.toml");
  ASSERT_TRUE(valid.ok());
  EXPECT_FALSE(valid.config->ui.top_bar_visible);
  EXPECT_EQ(valid.config->keybindings.toggle_top_bar, "Ctrl+L");
  EXPECT_TRUE(valid.config->keybindings.toggle_tab_bar.empty());

  for (const auto* shortcut : {"K", "ctrl+K", "Ctrl++K", "Ctrl+Escape"}) {
    const auto invalid = ParseConfig(
        std::string("schema_version=1\n[keybindings]\ntoggle_top_bar=\"") +
            shortcut +
            "\"\n[environments.personal]\ndata_directory=\"/tmp/p\"\n",
        "keys.toml");
    EXPECT_FALSE(invalid.ok()) << shortcut;
  }

  const auto conflict = ParseConfig(R"toml(
schema_version = 1
[keybindings]
toggle_top_bar = "Alt+H"
toggle_tab_bar = "Alt+H"
[environments.personal]
data_directory = "/tmp/personal"
)toml", "keys.toml");
  ASSERT_FALSE(conflict.ok());
  EXPECT_EQ(conflict.errors[0].key, "keybindings.toggle_tab_bar");
}

TEST(ConfigTest, RejectsDeepDottedKeysBeforeBuildingRecursiveTables) {
  std::string key = "a";
  for (int i = 1; i < 30000; ++i)
    key += ".a";
  for (const auto& document : {"[" + key + "]\nx=1\n", key + "=1\n"}) {
    const auto result = ParseConfig(document, "depth.toml");
    ASSERT_FALSE(result.ok());
    ASSERT_EQ(result.errors.size(), 1u);
    EXPECT_EQ(result.errors[0].key, "<toml>");
    EXPECT_NE(result.errors[0].message.find("key component count"), std::string::npos);
  }
}

TEST(ConfigTest, KeyComponentBoundDoesNotCountDotsInsideQuotedKeys) {
  std::string dots(30000, '.');
  const auto result = ParseConfig("[\"" + dots + "\"]\nx=1\n", "quoted.toml");
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.errors[0].key, "<toml>");
  EXPECT_EQ(result.errors[0].message, "unknown key");
}

TEST(ConfigTest, RejectsDeepNestedValues) {
  const auto result = ParseConfig("x=" + std::string(1000, '[') + "0" +
                                std::string(1000, ']'), "values.toml");
  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.errors.size(), 1u);
  EXPECT_NE(result.errors[0].message.find("nested value depth"), std::string::npos);
}

TEST(ConfigTest, RejectsC1ControlsInPathsAndUrls) {
  for (const auto* value : {"data_directory=\"/tmp/\\u0085\"",
                            "data_directory=\"/tmp/personal\"\nstartup_urls=[\"https://example.test/\\u0085\"]"}) {
    const auto result = ParseConfig(
        std::string("schema_version=1\n[environments.personal]\n") + value,
        "controls.toml");
    EXPECT_FALSE(result.ok());
  }
}

TEST(ConfigTest, ParsesMultilineTomlUrls) {
  const auto result = ParseConfig(R"toml(
schema_version = 1
[app]
default_environment = "work"
restore_last_environment = true
[ui]
sidebar_width = 126
theme = "dark"
[environments.work]
data_directory = "/tmp/work"
accent_color = "#1a2B3c"
startup_urls = [
  "https://example.test/path",
  "about:blank",
  "chrome://newtab",
]
)toml", "multi.toml");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.config->app.restore_last_environment);
  EXPECT_EQ(result.config->ui.sidebar_width, 126);
  ASSERT_EQ(result.config->environments[0].startup_urls.size(), 3u);
}

TEST(ConfigTest, RejectsUnknownKeysAtEveryTableLevel) {
  const auto result = ParseConfig(R"toml(
schema_version = 1
unexpected = 1
[ui]
unknown = true
[environments.personal]
data_directory = "/tmp/personal"
command = "do not accept"
)toml", "bad.toml");
  ASSERT_FALSE(result.ok());
  ASSERT_GE(result.errors.size(), 3u);
  EXPECT_EQ(result.errors[0].filename, "bad.toml");
  EXPECT_TRUE(std::any_of(result.errors.begin(), result.errors.end(),
                          [](const Diagnostic& error) {
                            return error.key == "unexpected";
                          }));
  EXPECT_TRUE(std::any_of(result.errors.begin(), result.errors.end(),
                          [](const Diagnostic& error) {
                            return error.key == "ui.unknown";
                          }));
  EXPECT_TRUE(std::any_of(result.errors.begin(), result.errors.end(),
                          [](const Diagnostic& error) {
                            return error.key == "environments.personal.command";
                          }));
}

TEST(ConfigTest, RejectsWrongTypesAndVersions) {
  const auto result = ParseConfig(R"toml(
schema_version = true
[ui]
sidebar_collapsed = 1
[environments.personal]
data_directory = 5
)toml", "types.toml");
  ASSERT_FALSE(result.ok());
  EXPECT_GE(result.errors.size(), 3u);
  EXPECT_EQ(result.errors[0].filename, "types.toml");
}

TEST(ConfigTest, RequiresSupportedSchemaVersion) {
  const auto result = ParseConfig(R"toml(
schema_version = 2
[environments.personal]
data_directory = "/tmp/personal"
)toml", "version.toml");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.errors[0].key, "schema_version");
  EXPECT_NE(result.errors[0].line, 0u);
}

TEST(ConfigTest, ValidatesEnvironmentNamesAndDefaultEnvironment) {
  const auto result = ParseConfig(R"toml(
schema_version = 1
[app]
default_environment = "missing"
[environments."bad name"]
data_directory = "/tmp/personal"
)toml", "environment.toml");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.errors[0].key, "environments.bad name");
  EXPECT_EQ(result.errors.back().key, "app.default_environment");
}

TEST(ConfigTest, ValidatesBoundsColorsAndUrls) {
  const auto result = ParseConfig(R"toml(
schema_version = 1
[ui]
sidebar_width = 401
[environments.personal]
data_directory = "/tmp/personal"
accent_color = "blue"
startup_urls = ["ftp://example.test", "https://user@example.test", "https://example.test/\u0001"]
)toml", "values.toml");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.errors.size(), 5u);
}

TEST(ConfigTest, RejectsBothSidebarWidthEndpointsOutsideNativeBounds) {
  const auto result = ParseConfig(R"toml(
schema_version = 1
[ui]
sidebar_width = 125
[environments.personal]
data_directory = "/tmp/personal"
)toml", "width.toml");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.errors[0].key, "ui.sidebar_width");
}

TEST(ConfigTest, RejectsMalformedTomlAndUnicode) {
  std::string malformed = "schema_version = 1\n[environments.personal]\ndata_directory = \"";
  malformed.push_back(static_cast<char>(0xff));
  malformed += "\"\n";
  const auto result = ParseConfig(malformed, "unicode.toml");
  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.errors.size(), 1u);
  EXPECT_EQ(result.errors[0].filename, "unicode.toml");
  EXPECT_EQ(result.errors[0].key, "<toml>");
  EXPECT_NE(result.errors[0].line, 0u);
}

TEST(ConfigTest, EnforcesConfigAndCollectionBounds) {
  std::string too_large(1024 * 1024 + 1, ' ');
  const auto large_result = ParseConfig(too_large, "large.toml");
  ASSERT_FALSE(large_result.ok());
  EXPECT_EQ(large_result.errors[0].key, "<document>");

  std::string urls = "schema_version = 1\n[environments.personal]\ndata_directory = \"/tmp/personal\"\nstartup_urls = [";
  for (int i = 0; i != 101; ++i) {
    if (i)
      urls += ',';
    urls += "\"https://example.test\"";
  }
  urls += "]\n";
  const auto url_result = ParseConfig(urls, "urls.toml");
  ASSERT_FALSE(url_result.ok());
  EXPECT_EQ(url_result.errors[0].key, "environments.personal.startup_urls");

  std::string environments = "schema_version = 1\n";
  for (int i = 0; i != 257; ++i) {
    environments += "[environments.e" + std::to_string(i) + "]\n";
    environments += "data_directory = \"/tmp/e\"\n";
  }
  const auto environment_result = ParseConfig(environments, "environments.toml");
  ASSERT_FALSE(environment_result.ok());
  EXPECT_EQ(environment_result.errors[0].key, "environments");
}

TEST(ConfigTest, IsPureAndDoesNotReadTheFilename) {
  const auto result = ParseConfig(kValidConfig, "/definitely/not/a/config.toml");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.config->environments[0].data_directory, "~/literal-$HOME");
}

}  // namespace
}  // namespace mb::config
