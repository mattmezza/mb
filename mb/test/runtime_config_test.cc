#include "mb/config/runtime_config.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "gtest/gtest.h"

namespace mb::config {
namespace {

class RuntimeConfigTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* base = std::getenv("MB_RUNTIME_CONFIG_TEST_TMPDIR");
    ASSERT_NE(base, nullptr);
    std::string pattern = std::string(base) + "/runtime-XXXXXX";
    char* created = mkdtemp(pattern.data());
    ASSERT_NE(created, nullptr);
    root_ = created;
    ASSERT_EQ(chmod(root_.c_str(), 0700), 0);
  }

  void TearDown() override {
    if (!root_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      EXPECT_FALSE(error) << error.message();
    }
  }

  std::string WriteConfig(const std::string& relative, const std::string& contents) {
    const std::string path = root_ + "/" + relative;
    const std::size_t slash = path.rfind('/');
    if (slash != std::string::npos) {
      std::error_code error;
      std::filesystem::create_directories(path.substr(0, slash), error);
      EXPECT_FALSE(error) << error.message();
      EXPECT_EQ(chmod(path.substr(0, slash).c_str(), 0700), 0);
    }
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    EXPECT_GE(fd, 0);
    if (fd < 0)
      return path;
    std::string_view remaining = contents;
    while (!remaining.empty()) {
      const ssize_t count = write(fd, remaining.data(), remaining.size());
      EXPECT_GT(count, 0);
      if (count <= 0)
        break;
      remaining.remove_prefix(static_cast<std::size_t>(count));
    }
    EXPECT_EQ(close(fd), 0);
    return path;
  }

  std::string Config(bool restore = true) const {
    return "schema_version = 1\n"
           "[app]\n"
           "default_environment = \"personal\"\n"
           "restore_last_environment = " + std::string(restore ? "true" : "false") + "\n"
           "[environments.personal]\n"
           "data_directory = \"roots/personal\"\n"
           "[environments.work]\n"
           "data_directory = \"~/roots/work\"\n";
  }

  std::string root_;
};

TEST_F(RuntimeConfigTest, SelectsExplicitThenRememberedThenConfiguredDefault) {
  const std::string config = WriteConfig("config/mb.toml", Config());

  auto explicit_result = LoadRuntimeConfig(config, root_, "work", "personal");
  ASSERT_TRUE(explicit_result.ok());
  EXPECT_EQ(explicit_result.snapshot->selected_environment, "work");
  EXPECT_EQ(explicit_result.snapshot->selected_root, root_ + "/roots/work");

  auto remembered_result = LoadRuntimeConfig(config, root_, std::nullopt, "work");
  ASSERT_TRUE(remembered_result.ok());
  EXPECT_EQ(remembered_result.snapshot->selected_environment, "work");

  auto default_result = LoadRuntimeConfig(config, root_, std::nullopt, std::nullopt);
  ASSERT_TRUE(default_result.ok());
  EXPECT_EQ(default_result.snapshot->selected_environment, "personal");
  EXPECT_EQ(default_result.snapshot->selected_root,
            root_ + "/config/roots/personal");
  EXPECT_EQ(access((root_ + "/config/roots/personal").c_str(), F_OK), -1);
  EXPECT_EQ(access((root_ + "/roots/work").c_str(), F_OK), -1);
}

TEST_F(RuntimeConfigTest, IgnoresRememberedValueWhenRestoreIsDisabled) {
  const std::string config = WriteConfig("mb.toml", Config(false));
  const auto result = LoadRuntimeConfig(config, root_, std::nullopt, "work");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.snapshot->selected_environment, "personal");
  EXPECT_TRUE(result.warnings.empty());
}

TEST_F(RuntimeConfigTest, ReportsStaleRememberedAndFallsBackToDefault) {
  const std::string config = WriteConfig("mb.toml", Config());
  const auto result = LoadRuntimeConfig(config, root_, std::nullopt, "removed");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.snapshot->selected_environment, "personal");
  ASSERT_EQ(result.warnings.size(), 1u);
  EXPECT_EQ(result.warnings[0].code,
            RuntimeConfigIssueCode::kStaleRememberedEnvironment);
  EXPECT_EQ(result.warnings[0].filename, result.snapshot->config_filename);
  EXPECT_EQ(result.warnings[0].key, "app.restore_last_environment");
  EXPECT_NE(result.warnings[0].message.find("falling back to configured default: personal"),
            std::string::npos);
}

TEST_F(RuntimeConfigTest, ReportsMalformedFilesAndUnknownExplicitSelection) {
  const std::string malformed = WriteConfig("malformed.toml", "schema_version =\n");
  const auto malformed_result = LoadRuntimeConfig(malformed, root_);
  ASSERT_FALSE(malformed_result.ok());
  ASSERT_EQ(malformed_result.errors.size(), 1u);
  EXPECT_EQ(malformed_result.errors[0].code, RuntimeConfigIssueCode::kConfigFile);
  EXPECT_EQ(malformed_result.errors[0].key, "<toml>");
  EXPECT_FALSE(malformed_result.errors[0].filename.empty());

  const std::string valid = WriteConfig("valid.toml", Config());
  const auto unknown_result = LoadRuntimeConfig(valid, root_, "missing");
  ASSERT_FALSE(unknown_result.ok());
  ASSERT_EQ(unknown_result.errors.size(), 1u);
  EXPECT_EQ(unknown_result.errors[0].code,
            RuntimeConfigIssueCode::kUnknownExplicitEnvironment);
  EXPECT_EQ(unknown_result.errors[0].filename,
            std::filesystem::canonical(valid).string());
  EXPECT_EQ(unknown_result.errors[0].key, "--environment");
  EXPECT_EQ(access((root_ + "/roots/personal").c_str(), F_OK), -1);
  EXPECT_EQ(access((root_ + "/roots/work").c_str(), F_OK), -1);
}

TEST_F(RuntimeConfigTest, CanonicalizesSymlinkedConfigurationBeforeResolvingRoots) {
  const std::string actual = WriteConfig("real/config.toml", Config());
  const std::string link = root_ + "/config-link.toml";
  ASSERT_EQ(symlink(actual.c_str(), link.c_str()), 0);

  const auto result = LoadRuntimeConfig(link, root_);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.snapshot->config_filename, std::filesystem::canonical(actual).string());
  EXPECT_EQ(result.snapshot->selected_root, root_ + "/real/roots/personal");
}

TEST_F(RuntimeConfigTest, AuditsUnselectedRootsAndNeverCreatesDirectories) {
  ASSERT_EQ(symlink(root_.c_str(), (root_ + "/unsafe-link").c_str()), 0);
  const std::string config = WriteConfig(
      "mb.toml", "schema_version = 1\n"
                 "[environments.personal]\n"
                 "data_directory = \"not-created/personal\"\n"
                 "[environments.unselected]\n"
                 "data_directory = \"unsafe-link/child\"\n");
  const auto result = LoadRuntimeConfig(config, root_);
  ASSERT_FALSE(result.ok());
  ASSERT_EQ(result.errors.size(), 1u);
  EXPECT_EQ(result.errors[0].code, RuntimeConfigIssueCode::kEnvironmentPath);
  EXPECT_EQ(result.errors[0].key, "environments.unselected.data_directory");
  EXPECT_EQ(access((root_ + "/not-created").c_str(), F_OK), -1);
  EXPECT_EQ(errno, ENOENT);
}

}  // namespace
}  // namespace mb::config
