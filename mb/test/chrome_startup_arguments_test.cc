// SPDX-License-Identifier: BSD-3-Clause
#include "mb/app/chrome_startup_arguments.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace mb {
namespace {

TEST(ChromeStartupArgumentsTest, BrowserUsesRawTokensBeforeDuplicateCollapse) {
  EXPECT_FALSE(NormalizeChromeStartupArguments(
                   {"mb", "--environment=work", "-environment", "home"}, {""})
                   .value);
  EXPECT_FALSE(NormalizeChromeStartupArguments({"mb", "--config"}, {""})
                   .value);
  auto result = NormalizeChromeStartupArguments(
      {"mb", "about:blank", "--environment", "work", "--", "--config"},
      {"", "--ozone-platform=x11"});
  ASSERT_TRUE(result.value) << result.error;
  EXPECT_EQ(result.value->argv,
            (std::vector<std::string>{"mb", "about:blank", "--environment=work",
                                      "--", "--config"}));
}

TEST(ChromeStartupArgumentsTest, ChildBypassesProductSelectors) {
  const std::vector<std::string> child = {
      "mb", "--type=renderer", "--config", "--user-data-dir=/tmp/child"};
  auto result = NormalizeChromeStartupArguments(
      child, {"", "--environment", "--config=duplicate", "--config=again"});
  ASSERT_TRUE(result.value) << result.error;
  EXPECT_EQ(result.value->argv, child);
  EXPECT_FALSE(result.value->config_path);
}

TEST(ChromeStartupArgumentsTest, RejectsSelectorBoundaryWhitespace) {
  for (const auto& argv : std::vector<std::vector<std::string>>{
           {"mb", "--config", "/tmp/config.toml "},
           {"mb", "--config", " /tmp/config.toml"},
           {"mb", "--config=/tmp/config.toml\t"},
           {"mb", " -config= /tmp/config.toml"},
           {"mb", "--environment", "work\n"},
           {"mb", "--environment= work"},
           {"mb", "-environment=work "}}) {
    EXPECT_FALSE(NormalizeChromeStartupArguments(argv, {""}).value);
  }
  EXPECT_TRUE(NormalizeChromeStartupArguments(
                  {"mb", "--config", "/tmp/path with spaces/config.toml"}, {""})
                  .value);
  EXPECT_TRUE(NormalizeChromeStartupArguments(
                  {"mb", "--", "--config=ordinary argument "}, {""})
                  .value);
}

TEST(ChromeStartupArgumentsTest, EmptyTypeCannotMasqueradeAsAChild) {
  for (const auto& argv : std::vector<std::vector<std::string>>{
           {"mb", "--type"},
           {"mb", "-type="},
           {"mb", "--type=\xc3\xa9"},
           {"mb", "--type=renderer", "--type="}}) {
    EXPECT_FALSE(NormalizeChromeStartupArguments(argv, {""}).value);
  }
  EXPECT_TRUE(NormalizeChromeStartupArguments({"mb", "--", "--type"}, {""})
                  .value);
}

TEST(ChromeStartupArgumentsTest, ExtraFlagsCannotInjectProductIdentity) {
  for (const auto& extra : std::vector<std::vector<std::string>>{
           {"", "--config=/tmp/config.toml"},
           {"", " --environment ", "work"},
           {"", "--environment=work", "-environment=home"},
           {"", "--environment"},
           {"", "-user-data-dir=/tmp/root"},
           {"", "--type=renderer"},
           {"", "--type="}}) {
    EXPECT_FALSE(NormalizeChromeStartupArguments({"mb"}, extra).value);
  }
  // The extra stream has its own terminator, just as in Chromium's existing
  // AppendArguments path; an unrelated raw terminator cannot conceal it.
  EXPECT_FALSE(NormalizeChromeStartupArguments(
                   {"mb", "--"}, {"", "--environment=work"})
                   .value);
  EXPECT_TRUE(NormalizeChromeStartupArguments(
                  {"mb"}, {"", "--", "--environment=ordinary-argument"})
                  .value);
}

TEST(ChromeStartupArgumentsTest, SecurityFlagsAreNotRemovedOrRootInjected) {
  const std::vector<std::string> raw = {
      "mb", "--disable-web-security", "--remote-debugging-pipe"};
  auto result = NormalizeChromeStartupArguments(
      raw, {"", "--disable-web-security", "--remote-debugging-pipe"});
  ASSERT_TRUE(result.value) << result.error;
  EXPECT_EQ(result.value->argv, raw);
  EXPECT_FALSE(result.value->has_user_data_dir);
}

}  // namespace
}  // namespace mb
