// SPDX-License-Identifier: BSD-3-Clause
#include "mb/browser/browser_config_gate_internal.h"
#include "mb/browser/browser_config_gate.h"

#include <optional>
#include <string>
#include <string_view>

#include "base/command_line.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/scoped_environment_variable_override.h"
#include "base/test/scoped_command_line.h"
#include "chrome/common/chrome_result_codes.h"
#include "mb/config/config.h"
#include "mb/config/runtime_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/url_util.h"

namespace mb::internal {
namespace {

TEST(BrowserConfigGateTest, ChromeUrlsBeforeContentSchemeRegistration) {
  url::ScopedSchemeRegistryForTests saved_registry;
  url::ClearSchemesForTests();

  EXPECT_TRUE(IsValidStartupUrl("chrome://newtab"));
  EXPECT_TRUE(IsValidStartupUrl("chrome://settings/appearance?test=1#section"));
  EXPECT_FALSE(IsValidStartupUrl("chrome://"));
  EXPECT_FALSE(IsValidStartupUrl("chrome://[invalid-host]/"));
  EXPECT_FALSE(IsValidStartupUrl("chrome://user@newtab"));
  EXPECT_FALSE(IsValidStartupUrl("chrome://user:password@newtab"));
  EXPECT_FALSE(IsValidStartupUrl("chrome://@newtab"));
  EXPECT_TRUE(IsValidStartupUrl("https://example.test/path"));
  EXPECT_TRUE(IsValidStartupUrl("about:blank"));
  // Registration must remain legal after validation, just as in Content startup.
  url::AddStandardScheme("mb-test", url::SCHEME_WITH_HOST);
  EXPECT_TRUE(url::IsStandard(std::string_view("mb-test")));
}

TEST(BrowserConfigGateTest, FullUrlValidationRejectsMalformedAuthority) {
  EXPECT_TRUE(IsValidStartupUrl("https://example.test/path"));
  EXPECT_TRUE(IsValidStartupUrl("about:blank"));
  EXPECT_FALSE(IsValidStartupUrl("https://[invalid-host]/"));
  EXPECT_FALSE(IsValidStartupUrl("https://example.test:invalid-port/"));
  EXPECT_FALSE(IsValidStartupUrl("https://user:password@example.test/"));
  EXPECT_FALSE(IsValidStartupUrl("javascript:alert(1)"));
}

TEST(BrowserConfigGateTest, NativeRootMustBeAbsoluteAndMatchSelectedRoot) {
  config::RuntimeConfig snapshot;
  snapshot.config_filename = "/tmp/mb-config.toml";
  snapshot.selected_root = "/tmp/mb-work-private";
  const std::string home = "/tmp/mb-home";

  EXPECT_TRUE(NativeRootMatches(snapshot, home,
                               base::FilePath("/tmp/mb-work-private")));
  EXPECT_TRUE(NativeRootMatches(snapshot, home,
                               base::FilePath("/tmp//mb-work-private/./")));
  EXPECT_FALSE(NativeRootMatches(snapshot, home,
                                base::FilePath("mb-work-private")));
  EXPECT_FALSE(NativeRootMatches(snapshot, home,
                                base::FilePath("/tmp/mb-other-private")));
  EXPECT_FALSE(NativeRootMatches(snapshot, home,
                                base::FilePath("/tmp/mb-work-private/child")));
  EXPECT_FALSE(NativeRootMatches(snapshot, home,
                                base::FilePath("/tmp/../tmp/mb-work-private")));
}

TEST(BrowserConfigGateTest, SchemaDiagnosticsRetainExpectedTypeRangeAndKey) {
  struct Case {
    const char* ui_setting;
    const char* expected_key;
    const char* expected_reason;
  };
  for (const auto& test : {
           Case{"sidebar_width = 'wide'", "ui.sidebar_width",
                "must be an integer"},
           Case{"sidebar_width = 10", "ui.sidebar_width",
                "must be between 126 and 400"},
           Case{"unexpected = true", "ui.unexpected", "unknown key"}}) {
    const std::string input =
        std::string("schema_version = 1\n[ui]\n") + test.ui_setting +
        "\n[environments.personal]\ndata_directory = 'profiles/personal'\n";
    const auto parsed = config::ParseConfig(input, "/tmp/mb-config.toml");
    ASSERT_FALSE(parsed.ok());
    ASSERT_EQ(parsed.errors.size(), 1u);
    const auto& error = parsed.errors.front();
    const config::RuntimeConfigIssue issue = {
        config::RuntimeConfigIssueCode::kConfigFile, error.filename, error.key,
        error.message, error.line, 0};
    const std::string diagnostic = FormatRuntimeConfigIssue(issue);
    EXPECT_NE(diagnostic.find(test.expected_key), std::string::npos);
    EXPECT_NE(diagnostic.find(test.expected_reason), std::string::npos);
    EXPECT_NE(diagnostic.find("line 3"), std::string::npos);
  }
}

TEST(BrowserConfigGateTest, PathDiagnosticDetailIsEscapedAndBounded) {
  config::RuntimeConfigIssue issue = {
      config::RuntimeConfigIssueCode::kEnvironmentPath,
      "/tmp/config\n.toml",
      "environments.personal.data_directory\t",
      "Cannot open directory component: private\nname\x1b[0m",
      0,
      13};
  const std::string diagnostic = FormatRuntimeConfigIssue(issue);
  EXPECT_NE(diagnostic.find("Cannot open directory component: private"),
            std::string::npos);
  EXPECT_NE(diagnostic.find("\\x0aname\\x1b[0m"), std::string::npos);
  EXPECT_NE(diagnostic.find("\\x09"), std::string::npos);
  EXPECT_NE(diagnostic.find("errno 13"), std::string::npos);
  EXPECT_EQ(diagnostic.find('\n'), std::string::npos);
  EXPECT_EQ(diagnostic.find('\x1b'), std::string::npos);

  issue.message = std::string(10000, 'x');
  const auto bounded = FormatRuntimeConfigIssue(issue);
  EXPECT_LT(bounded.size(), 4500u);
  EXPECT_NE(bounded.find("..."), std::string::npos);
}

TEST(BrowserConfigGateTest, RawTomlDescriptionDoesNotLeakInputValues) {
  const config::RuntimeConfigIssue issue = {
      config::RuntimeConfigIssueCode::kConfigFile,
      "/tmp/mb-config.toml",
      "<toml>",
      "Unexpected value SECRET_VALUE https://private.invalid/?token=secret",
      7,
      0};
  const std::string diagnostic = FormatRuntimeConfigIssue(issue);
  EXPECT_NE(diagnostic.find("invalid TOML syntax"), std::string::npos);
  EXPECT_NE(diagnostic.find("line 7"), std::string::npos);
  EXPECT_EQ(diagnostic.find("SECRET_VALUE"), std::string::npos);
  EXPECT_EQ(diagnostic.find("private.invalid"), std::string::npos);
}

TEST(BrowserConfigGateTest, NativeNoConfigUserDataDirBypassesXdg) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath home = directory.GetPath().AppendASCII("home");
  ASSERT_TRUE(base::CreateDirectory(home));
  const base::FilePath config = directory.GetPath().AppendASCII("xdg-config");
  const base::FilePath state = directory.GetPath().AppendASCII("xdg-state");
  const base::FilePath data = directory.GetPath().AppendASCII("xdg-data");
  const base::FilePath native = directory.GetPath().AppendASCII("native");
  base::ScopedEnvironmentVariableOverride home_override("HOME", home.value());
  base::ScopedEnvironmentVariableOverride config_override("XDG_CONFIG_HOME",
                                                          config.value());
  base::ScopedEnvironmentVariableOverride state_override("XDG_STATE_HOME",
                                                         state.value());
  base::ScopedEnvironmentVariableOverride data_override("XDG_DATA_HOME",
                                                        data.value());
  base::ScopedEnvironmentVariableOverride root_override("CHROME_USER_DATA_DIR");
  ASSERT_TRUE(home_override.IsOverridden());
  ASSERT_TRUE(config_override.IsOverridden());
  ASSERT_TRUE(state_override.IsOverridden());
  ASSERT_TRUE(data_override.IsOverridden());
  ASSERT_TRUE(root_override.IsOverridden());
  base::test::ScopedCommandLine command_line;
  *command_line.GetProcessCommandLine() =
      base::CommandLine(base::CommandLine::NO_PROGRAM);
  command_line.GetProcessCommandLine()->AppendSwitchPath("user-data-dir",
                                                         native);

  ASSERT_EQ(GetProcessRuntimeConfig(), nullptr);
  EXPECT_EQ(InitializeExplicitBrowserConfig(), std::nullopt);
  EXPECT_FALSE(base::PathExists(config));
  EXPECT_FALSE(base::PathExists(state));
  EXPECT_FALSE(base::PathExists(data));
  EXPECT_FALSE(base::PathExists(native));
  EXPECT_EQ(GetProcessRuntimeConfig(), nullptr);
}

TEST(BrowserConfigGateTest, SelectorPreventsNativeNoConfigBypass) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath home = directory.GetPath().AppendASCII("home");
  ASSERT_TRUE(base::CreateDirectory(home));
  const base::FilePath config = directory.GetPath().AppendASCII("xdg-config");
  const base::FilePath state = directory.GetPath().AppendASCII("xdg-state");
  const base::FilePath data = directory.GetPath().AppendASCII("xdg-data");
  const base::FilePath native = directory.GetPath().AppendASCII("native");
  base::ScopedEnvironmentVariableOverride home_override("HOME", home.value());
  base::ScopedEnvironmentVariableOverride config_override("XDG_CONFIG_HOME",
                                                          config.value());
  base::ScopedEnvironmentVariableOverride state_override("XDG_STATE_HOME",
                                                         state.value());
  base::ScopedEnvironmentVariableOverride data_override("XDG_DATA_HOME",
                                                        data.value());
  base::ScopedEnvironmentVariableOverride root_override("CHROME_USER_DATA_DIR");
  ASSERT_TRUE(home_override.IsOverridden());
  ASSERT_TRUE(config_override.IsOverridden());
  ASSERT_TRUE(state_override.IsOverridden());
  ASSERT_TRUE(data_override.IsOverridden());
  ASSERT_TRUE(root_override.IsOverridden());
  base::test::ScopedCommandLine command_line;
  *command_line.GetProcessCommandLine() =
      base::CommandLine(base::CommandLine::NO_PROGRAM);
  command_line.GetProcessCommandLine()->AppendSwitchASCII("environment", "work");
  command_line.GetProcessCommandLine()->AppendSwitchPath("user-data-dir",
                                                         native);

  ASSERT_EQ(GetProcessRuntimeConfig(), nullptr);
  EXPECT_EQ(InitializeExplicitBrowserConfig(),
            std::optional<int>(CHROME_RESULT_CODE_UNSUPPORTED_PARAM));
  EXPECT_TRUE(base::PathExists(config));
  EXPECT_FALSE(base::PathExists(native));
  EXPECT_EQ(
      command_line.GetProcessCommandLine()->GetSwitchValuePath("user-data-dir"),
      native);
  EXPECT_EQ(GetProcessRuntimeConfig(), nullptr);
}

TEST(BrowserConfigGateTest, InvalidUnselectedUrlFailsBeforeSelectedRootCreation) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath filename = directory.GetPath().AppendASCII("config.toml");
  ASSERT_TRUE(base::WriteFile(filename, R"toml(schema_version = 1
[environments.personal]
data_directory = 'selected'
[environments.work]
data_directory = 'unselected'
startup_urls = ['https://example.test:invalid-port/']
)toml"));
  const auto parsed = config::LoadRuntimeConfig(
      filename.value(), directory.GetPath().value());
  ASSERT_TRUE(parsed.ok());  // Standalone lexical validation is insufficient.

  base::ScopedEnvironmentVariableOverride home(
      "HOME", directory.GetPath().value());
  base::ScopedEnvironmentVariableOverride root_override("CHROME_USER_DATA_DIR");
  ASSERT_TRUE(home.IsOverridden());
  ASSERT_TRUE(root_override.IsOverridden());
  base::test::ScopedCommandLine command_line;
  *command_line.GetProcessCommandLine() =
      base::CommandLine(base::CommandLine::NO_PROGRAM);
  command_line.GetProcessCommandLine()->AppendSwitchPath("config", filename);
  ASSERT_EQ(GetProcessRuntimeConfig(), nullptr);
  EXPECT_EQ(InitializeExplicitBrowserConfig(),
            std::optional<int>(CHROME_RESULT_CODE_UNSUPPORTED_PARAM));
  EXPECT_FALSE(base::PathExists(directory.GetPath().AppendASCII("selected")));
  EXPECT_FALSE(base::PathExists(directory.GetPath().AppendASCII("unselected")));
  EXPECT_FALSE(command_line.GetProcessCommandLine()->HasSwitch("user-data-dir"));
  EXPECT_EQ(GetProcessRuntimeConfig(), nullptr);
}

TEST(BrowserConfigGateTest, MismatchedNativeRootFailsBeforeSelectedRootCreation) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());
  const base::FilePath filename = directory.GetPath().AppendASCII("config.toml");
  ASSERT_TRUE(base::WriteFile(filename, R"toml(schema_version = 1
[environments.personal]
data_directory = 'selected'
)toml"));
  ASSERT_TRUE(config::LoadRuntimeConfig(filename.value(),
                                        directory.GetPath().value())
                  .ok());
  const base::FilePath supplied = directory.GetPath().AppendASCII("different");
  base::ScopedEnvironmentVariableOverride home(
      "HOME", directory.GetPath().value());
  base::ScopedEnvironmentVariableOverride root_override("CHROME_USER_DATA_DIR");
  ASSERT_TRUE(home.IsOverridden());
  ASSERT_TRUE(root_override.IsOverridden());
  base::test::ScopedCommandLine command_line;
  *command_line.GetProcessCommandLine() =
      base::CommandLine(base::CommandLine::NO_PROGRAM);
  command_line.GetProcessCommandLine()->AppendSwitchPath("config", filename);
  command_line.GetProcessCommandLine()->AppendSwitchPath("user-data-dir", supplied);
  ASSERT_EQ(GetProcessRuntimeConfig(), nullptr);
  EXPECT_EQ(InitializeExplicitBrowserConfig(),
            std::optional<int>(CHROME_RESULT_CODE_UNSUPPORTED_PARAM));
  EXPECT_FALSE(base::PathExists(directory.GetPath().AppendASCII("selected")));
  EXPECT_FALSE(base::PathExists(supplied));
  EXPECT_EQ(command_line.GetProcessCommandLine()->GetSwitchValuePath(
                "user-data-dir"),
            supplied);
  EXPECT_EQ(GetProcessRuntimeConfig(), nullptr);
}

}  // namespace
}  // namespace mb::internal
