#include "mb/app/startup_arguments.h"

#include "gtest/gtest.h"

namespace mb {
namespace {

TEST(StartupArgumentsTest, NormalizesSelectorsWithoutReorderingUrlsOrSwitches) {
  auto parsed = NormalizeStartupArguments(
      {"browser", "https://first.test", "--environment", "work", "--incognito",
       "--config", "/path with spaces/config.toml", "https://second.test"});
  ASSERT_TRUE(parsed.value) << parsed.error;
  EXPECT_EQ(parsed.value->environment, "work");
  EXPECT_EQ(parsed.value->config_path, "/path with spaces/config.toml");
  EXPECT_EQ(parsed.value->argv,
            (std::vector<std::string>{"browser", "https://first.test",
             "--environment=work", "--incognito",
             "--config=/path with spaces/config.toml", "https://second.test"}));
}

TEST(StartupArgumentsTest, PreservesUnknownFlagsAndTerminators) {
  const std::vector<std::string> argv = {
      "browser", "--config-extra=value", "--some-chromium-flag", "",
      "--", "--config", "--environment=literal", "--user-data-dir=/literal"};
  auto parsed = NormalizeStartupArguments(argv);
  ASSERT_TRUE(parsed.value) << parsed.error;
  EXPECT_EQ(parsed.value->argv, argv);
  EXPECT_FALSE(parsed.value->environment);
  EXPECT_FALSE(parsed.value->config_path);
  EXPECT_FALSE(parsed.value->has_user_data_dir);
}

TEST(StartupArgumentsTest, EqualsSyntaxPreservesLeadingDashAndEquals) {
  auto parsed = NormalizeStartupArguments(
      {"browser", "--config=-file=name.toml", "--environment=personal"});
  ASSERT_TRUE(parsed.value) << parsed.error;
  EXPECT_EQ(parsed.value->config_path, "-file=name.toml");
  EXPECT_EQ(parsed.value->environment, "personal");
}

TEST(StartupArgumentsTest, DuplicateSelectorsFailWithoutPartialResult) {
  for (const auto* selector : {"--config", "--environment"}) {
    for (const auto& duplicate : {std::string(selector), std::string(selector) + "=second"}) {
      auto parsed = NormalizeStartupArguments({"browser", selector, "first", duplicate, "second"});
      EXPECT_FALSE(parsed.value);
      EXPECT_NE(parsed.error.find(selector), std::string::npos);
      EXPECT_NE(parsed.error.find("more than once"), std::string::npos);
    }
  }
}

TEST(StartupArgumentsTest, MissingAndEmptyValuesFail) {
  for (const auto* selector : {"--config", "--environment"}) {
    for (const auto& argv : std::vector<std::vector<std::string>>{
             {"browser", selector}, {"browser", std::string(selector) + "="},
             {"browser", selector, ""}, {"browser", selector, "--"},
             {"browser", selector, "--incognito"}}) {
      auto parsed = NormalizeStartupArguments(argv);
      EXPECT_FALSE(parsed.value);
      EXPECT_NE(parsed.error.find(selector), std::string::npos);
    }
  }
}

TEST(StartupArgumentsTest, RootPresenceIsReportedForBrowserOnlyPolicy) {
  for (const auto* token : {"--user-data-dir", "--user-data-dir=/root"}) {
    auto parsed = NormalizeStartupArguments({"browser", "--type=renderer", token});
    ASSERT_TRUE(parsed.value) << parsed.error;
    EXPECT_TRUE(parsed.value->has_user_data_dir);
  }
  auto parsed = NormalizeStartupArguments({"browser", "--user-data-directory=/root"});
  ASSERT_TRUE(parsed.value);
  EXPECT_FALSE(parsed.value->has_user_data_dir);
}

TEST(StartupArgumentsTest, RejectsMalformedArgv) {
  EXPECT_FALSE(NormalizeStartupArguments({}).value);
  EXPECT_FALSE(NormalizeStartupArguments({""}).value);
  EXPECT_FALSE(NormalizeStartupArguments({"browser", std::string("a\0b", 3)}).value);
}

TEST(StartupArgumentsTest, MatchesChromiumPosixAliasesAndWhitespace) {
  auto parsed = NormalizeStartupArguments(
      {"browser", " -config=/tmp/config.toml ", "-environment", "work",
       "\t-user-data-dir=/tmp/root\t", " -- ", "--environment=literal"});
  ASSERT_TRUE(parsed.value) << parsed.error;
  EXPECT_EQ(parsed.value->config_path, "/tmp/config.toml");
  EXPECT_EQ(parsed.value->environment, "work");
  EXPECT_TRUE(parsed.value->has_user_data_dir);
  EXPECT_EQ(parsed.value->argv[1], "--config=/tmp/config.toml");
  EXPECT_EQ(parsed.value->argv.back(), "--environment=literal");
  EXPECT_FALSE(NormalizeStartupArguments(
      {"browser", "--environment=work", " -environment=personal "}).value);
  EXPECT_FALSE(NormalizeStartupArguments(
      {"browser", "--config", " --incognito "}).value);
}

}  // namespace
}  // namespace mb
