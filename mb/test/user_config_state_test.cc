// SPDX-License-Identifier: BSD-3-Clause
#include "mb/browser/user_config_state.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"
#include "mb/config/config.h"

namespace mb {
namespace {

class UserConfigStateTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* base = std::getenv("MB_USER_CONFIG_STATE_TEST_TMPDIR");
    std::string pattern =
        std::string(base ? base : "/tmp") + "/user-config-state-XXXXXX";
    char* created = mkdtemp(pattern.data());
    ASSERT_NE(created, nullptr);
    root_ = created;
    home_ = root_ + "/home";
    ASSERT_EQ(mkdir(home_.c_str(), 0700), 0);
    ASSERT_TRUE(ResolveUserConfigPaths(
        home_, root_ + "/xdg-config", root_ + "/xdg-state", root_ + "/xdg-data",
        "mb", &paths_, &error_))
        << error_.message;
  }

  void TearDown() override {
    if (!root_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      EXPECT_FALSE(error) << error.message();
    }
  }

  std::string ReadFile(const std::string& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
  }

  mode_t Mode(const std::string& path) {
    struct stat st = {};
    EXPECT_EQ(stat(path.c_str(), &st), 0);
    return st.st_mode & 0777;
  }

  std::string root_;
  std::string home_;
  UserConfigPaths paths_;
  UserConfigStateError error_;
};

TEST_F(UserConfigStateTest, BootstrapsOnlyPrivateConfigAndNoEnvironmentRoot) {
  bool created = false;
  ASSERT_TRUE(EnsureDefaultConfig(paths_, home_, &created, &error_))
      << error_.message;
  EXPECT_TRUE(created);
  EXPECT_EQ(Mode(paths_.config_directory), 0700u);
  EXPECT_EQ(Mode(paths_.config_file), 0600u);
  EXPECT_FALSE(std::filesystem::exists(paths_.default_environment_root));

  const auto parsed =
      config::ParseConfig(ReadFile(paths_.config_file), paths_.config_file);
  ASSERT_TRUE(parsed.ok());
  ASSERT_EQ(parsed.config->environments.size(), 1u);
  EXPECT_EQ(parsed.config->environments[0].name, "personal");
  EXPECT_EQ(parsed.config->environments[0].data_directory,
            paths_.default_environment_root);

  const std::string original = ReadFile(paths_.config_file);
  ASSERT_TRUE(EnsureDefaultConfig(paths_, home_, &created, &error_))
      << error_.message;
  EXPECT_FALSE(created);
  EXPECT_EQ(ReadFile(paths_.config_file), original);
}

TEST_F(UserConfigStateTest, ExistingConfigIsNeverReplaced) {
  std::filesystem::create_directories(paths_.config_directory);
  ASSERT_EQ(chmod(paths_.config_directory.c_str(), 0700), 0);
  {
    std::ofstream output(paths_.config_file);
    output << "existing data\n";
  }
  ASSERT_EQ(chmod(paths_.config_file.c_str(), 0600), 0);

  bool created = true;
  ASSERT_TRUE(EnsureDefaultConfig(paths_, home_, &created, &error_))
      << error_.message;
  EXPECT_FALSE(created);
  EXPECT_EQ(ReadFile(paths_.config_file), "existing data\n");
}

TEST_F(UserConfigStateTest, RefusesSymlinkInsteadOfReplacingConfig) {
  std::filesystem::create_directories(paths_.config_directory);
  ASSERT_EQ(chmod(paths_.config_directory.c_str(), 0700), 0);
  const std::string target = root_ + "/target.toml";
  {
    std::ofstream output(target);
    output << "do not replace\n";
  }
  ASSERT_EQ(chmod(target.c_str(), 0600), 0);
  ASSERT_EQ(symlink(target.c_str(), paths_.config_file.c_str()), 0);

  bool created = false;
  EXPECT_FALSE(EnsureDefaultConfig(paths_, home_, &created, &error_));
  EXPECT_FALSE(created);
  EXPECT_EQ(ReadFile(target), "do not replace\n");
}

TEST_F(UserConfigStateTest,
       StateReadDoesNotCreateAndWriteRoundTripsAtomically) {
  std::optional<std::string> remembered;
  ASSERT_TRUE(ReadRememberedEnvironment(paths_, home_, &remembered, &error_))
      << error_.message;
  EXPECT_FALSE(remembered.has_value());
  EXPECT_FALSE(std::filesystem::exists(paths_.state_directory));

  ASSERT_TRUE(WriteRememberedEnvironment(paths_, home_, "work", &error_))
      << error_.message;
  EXPECT_EQ(Mode(paths_.state_directory), 0700u);
  EXPECT_EQ(Mode(paths_.state_file), 0600u);
  ASSERT_TRUE(ReadRememberedEnvironment(paths_, home_, &remembered, &error_))
      << error_.message;
  ASSERT_TRUE(remembered.has_value());
  EXPECT_EQ(*remembered, "work");
  EXPECT_EQ(ReadFile(paths_.state_file),
            "schema_version = 1\nlast_environment = \"work\"\n");
}

TEST_F(UserConfigStateTest,
       RejectsUnsafeOrInvalidStateWithoutCreatingAnything) {
  std::optional<std::string> remembered;
  ASSERT_TRUE(WriteRememberedEnvironment(paths_, home_, "work", &error_))
      << error_.message;
  {
    std::ofstream output(paths_.state_file, std::ios::trunc);
    output << "schema_version = 2\nlast_environment = \"work\"\n";
  }
  ASSERT_EQ(chmod(paths_.state_file.c_str(), 0600), 0);
  EXPECT_FALSE(ReadRememberedEnvironment(paths_, home_, &remembered, &error_));
  EXPECT_EQ(error_.key, "state.toml");

  ASSERT_EQ(chmod(paths_.state_file.c_str(), 0644), 0);
  EXPECT_FALSE(ReadRememberedEnvironment(paths_, home_, &remembered, &error_));
  EXPECT_EQ(error_.key, "state.toml");
}

TEST_F(UserConfigStateTest, EmptyXdgUsesFallbackAndRelativeValuesAreRejected) {
  UserConfigPaths paths;
  ASSERT_TRUE(ResolveUserConfigPaths(home_, std::string(), std::string(),
                                     std::string(), "mb", &paths, &error_))
      << error_.message;
  EXPECT_EQ(paths.config_file, home_ + "/.config/mb/config.toml");
  EXPECT_EQ(paths.state_file, home_ + "/.local/state/mb/state.toml");
  EXPECT_EQ(paths.default_environment_root,
            home_ + "/.local/share/mb/environments/personal");

  EXPECT_FALSE(ResolveUserConfigPaths(home_, "relative", root_ + "/state",
                                      root_ + "/data", "mb", &paths, &error_));
  EXPECT_EQ(error_.key, "XDG_CONFIG_HOME");
  EXPECT_FALSE(ResolveUserConfigPaths(home_, "~/config", root_ + "/state",
                                      root_ + "/data", "mb", &paths, &error_));
  EXPECT_EQ(error_.key, "XDG_CONFIG_HOME");
  EXPECT_FALSE(ResolveUserConfigPaths(home_, root_ + "/config",
                                      root_ + "/state", "relative", "mb",
                                      &paths, &error_));
  EXPECT_EQ(error_.key, "XDG_DATA_HOME");
  EXPECT_FALSE(ResolveUserConfigPaths(home_, root_ + "/config",
                                      root_ + "/state", root_ + "/data",
                                      "../mb", &paths, &error_));
  EXPECT_EQ(error_.key, "profile_directory");
}

}  // namespace
}  // namespace mb
