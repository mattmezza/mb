#include "mb/browser/environment_paths.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace mb {
namespace {

std::string AbsolutePathOfLength(size_t length) {
  std::string path;
  while (path.size() + 2 <= length) path += "/a";
  if (path.size() < length) path += "a";
  return path;
}

TEST(EnvironmentPathResolution, EnforcesPathAndComponentByteLimits) {
  EnvironmentDirectories paths;
  EnvironmentPathError error;
  const std::string longest = AbsolutePathOfLength(PATH_MAX - 1);
  ASSERT_TRUE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
      {{"work", longest}}, &paths, &error)) << error.message;
  EXPECT_EQ(paths.at("work").size(), PATH_MAX - 1u);
  // Limits apply after lexical normalization, which removes extra separators.
  ASSERT_TRUE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
      {{"work", longest + "////./"}}, &paths, &error)) << error.message;
  EXPECT_EQ(paths.at("work"), longest);
  ASSERT_TRUE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
      {{"work", "/" + std::string(NAME_MAX, 'a')}}, &paths, &error));
  for (const auto& raw : {AbsolutePathOfLength(PATH_MAX),
                          "/" + std::string(NAME_MAX + 1, 'a')}) {
    EXPECT_FALSE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
        {{"work", raw}}, &paths, &error));
    EXPECT_EQ(error.code, EnvironmentPathErrorCode::kInvalidPath);
    EXPECT_EQ(error.key, "environments.work.data_directory");
    EXPECT_TRUE(paths.empty());
  }
  std::string multibyte = "/";
  for (int i = 0; i <= NAME_MAX / 2; ++i) multibyte += "é";
  EXPECT_FALSE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
      {{"work", multibyte}}, &paths, &error));
}

TEST(EnvironmentPathResolution, BoundsExpandedRootsAndContextPaths) {
  EnvironmentDirectories paths;
  EnvironmentPathError error;
  const std::string longest = AbsolutePathOfLength(PATH_MAX - 1);
  EXPECT_FALSE(ResolveEnvironmentPaths("/config/mb.toml", longest,
      {{"work", "~/child"}}, &paths, &error));
  EXPECT_EQ(error.key, "environments.work.data_directory");
  EXPECT_FALSE(ResolveEnvironmentPaths(longest, "/home/user",
      {{"work", "child"}}, &paths, &error));
  EXPECT_EQ(error.key, "environments.work.data_directory");
  EXPECT_FALSE(ResolveEnvironmentPaths(AbsolutePathOfLength(PATH_MAX), "/home/user",
      {}, &paths, &error));
  EXPECT_EQ(error.key, "config_file");
  EXPECT_FALSE(ResolveEnvironmentPaths("/config/mb.toml", AbsolutePathOfLength(PATH_MAX),
      {}, &paths, &error));
  EXPECT_EQ(error.key, "home");
}

TEST(EnvironmentPathResolution, ExpandsAbsoluteRelativeAndHomePaths) {
  EnvironmentDirectories paths;
  EnvironmentPathError error;
  ASSERT_TRUE(ResolveEnvironmentPaths(
      "/home/user/config/mb.toml", "/home/user",
      {{"absolute", "/data/work//./"}, {"relative", "roots/personal"},
       {"home", "~/browser/work"}}, &paths, &error)) << error.message;
  EXPECT_EQ(paths.at("absolute"), "/data/work");
  EXPECT_EQ(paths.at("relative"), "/home/user/config/roots/personal");
  EXPECT_EQ(paths.at("home"), "/home/user/browser/work");
}

TEST(EnvironmentPathResolution, RejectsUnsafeStringsAndTraversal) {
  const std::vector<std::string> bad = {
      "", "~", "~other/work", "../escape", "/a/../b", "~/../escape", "/",
      "/hello\nworld", std::string("/a\0b", 4), std::string("/\xc0\xaf", 3),
      std::string("/\xed\xa0\x80", 4), std::string("/\xf4\x90\x80\x80", 5),
      std::string("/\xc2\x85", 3), std::string("/\xe2", 2)};
  for (const auto& raw : bad) {
    EnvironmentDirectories paths = {{"old", "/old"}};
    EnvironmentPathError error;
    EXPECT_FALSE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
                                        {{"work", raw}}, &paths, &error));
    EXPECT_TRUE(paths.empty());
    EXPECT_EQ(error.key, "environments.work.data_directory");
    EXPECT_EQ(error.code, EnvironmentPathErrorCode::kInvalidPath);
  }
}

TEST(EnvironmentPathResolution, NormalizedAliasesAndNestedRootsAreRejected) {
  for (const auto& second : {"/data/./a//", "/data/a/child", "/data"}) {
    EnvironmentDirectories paths;
    EnvironmentPathError error;
    EXPECT_FALSE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
                                        {{"first", "/data/a"}, {"second", second}},
                                        &paths, &error));
    EXPECT_EQ(error.code, EnvironmentPathErrorCode::kOverlappingRoots);
    EXPECT_NE(error.message.find("environments.first.data_directory"), std::string::npos);
  }
}

TEST(EnvironmentPathResolution, PrefixSiblingsAndLiteralShellSyntaxAreAllowed) {
  EnvironmentDirectories paths;
  EnvironmentPathError error;
  ASSERT_TRUE(ResolveEnvironmentPaths("/config/mb.toml", "/home/user",
      {{"one", "/data/a"}, {"two", "/data/ab"},
       {"literal", "$HOME/$(touch marker)"}, {"unicode", "/data/日本語"}},
      &paths, &error));
  EXPECT_EQ(paths.at("literal"), "/config/$HOME/$(touch marker)");
}

TEST(EnvironmentPathResolution, RejectsInvalidConfigurationAndHomeInputs) {
  EnvironmentDirectories paths;
  EnvironmentPathError error;
  EXPECT_FALSE(ResolveEnvironmentPaths("relative.toml", "/home/user", {}, &paths, &error));
  EXPECT_EQ(error.key, "config_file");
  EXPECT_FALSE(ResolveEnvironmentPaths("/config/", "/home/user", {}, &paths, &error));
  EXPECT_FALSE(ResolveEnvironmentPaths("/config.toml", "relative", {}, &paths, &error));
  EXPECT_EQ(error.key, "home");
}

class EnvironmentPreparation : public testing::Test {
 protected:
  void SetUp() override {
    const char* base = std::getenv("MB_ENVIRONMENT_TEST_TMPDIR");
    ASSERT_NE(base, nullptr);
    std::string pattern = std::string(base) + "/paths-XXXXXX";
    char* created = mkdtemp(pattern.data());
    ASSERT_NE(created, nullptr);
    root_ = created;
  }
  void TearDown() override {
    if (!root_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(root_, error);
      EXPECT_FALSE(error) << error.message();
    }
  }
  bool Prepare(const EnvironmentDirectories& paths, const std::string& selected = "work") {
    return PrepareEnvironmentRoot(root_ + "/config/mb.toml", root_, paths,
                                  selected, &prepared_, &error_);
  }
  mode_t Mode(const std::string& path) {
    struct stat st = {};
    EXPECT_EQ(stat(path.c_str(), &st), 0);
    return st.st_mode & 0777;
  }
  std::string root_;
  PreparedEnvironmentRoot prepared_;
  EnvironmentPathError error_;
};

TEST_F(EnvironmentPreparation, CreatesOnlySelectedRootAndPrivateParents) {
  ASSERT_TRUE(Prepare({{"work", root_ + "/new/parents/work"},
                       {"personal", root_ + "/unused/personal"}})) << error_.message;
  EXPECT_EQ(Mode(root_ + "/new"), 0700u);
  EXPECT_EQ(Mode(root_ + "/new/parents"), 0700u);
  EXPECT_EQ(Mode(prepared_.path()), 0700u);
  EXPECT_FALSE(std::filesystem::exists(root_ + "/unused"));
  EXPECT_GE(prepared_.directory_fd(), 0);
  EXPECT_NE(fcntl(prepared_.directory_fd(), F_GETFD) & FD_CLOEXEC, 0);
}

TEST_F(EnvironmentPreparation, PureResolutionNeverCreatesDirectories) {
  EnvironmentDirectories paths;
  ASSERT_TRUE(ResolveEnvironmentPaths(root_ + "/config/mb.toml", root_,
      {{"work", "roots/work"}}, &paths, &error_));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/config"));
}

TEST_F(EnvironmentPreparation, AuditResolvesMissingPathsWithoutCreatingThem) {
  EnvironmentDirectories paths;
  ASSERT_TRUE(AuditEnvironmentPaths(root_ + "/config/mb.toml", root_,
      {{"work", "roots/work"}, {"personal", "~/personal"}}, &paths, &error_))
      << error_.message;
  EXPECT_EQ(paths.at("work"), root_ + "/config/roots/work");
  EXPECT_EQ(paths.at("personal"), root_ + "/personal");
  EXPECT_FALSE(std::filesystem::exists(root_ + "/config"));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/personal"));
}

TEST_F(EnvironmentPreparation, AuditAndPreparationRejectSameUnsafeFilesystem) {
  ASSERT_EQ(mkdir((root_ + "/unsafe").c_str(), 0755), 0);
  ASSERT_EQ(chmod((root_ + "/unsafe").c_str(), 0755), 0);
  ASSERT_EQ(symlink(root_.c_str(), (root_ + "/link").c_str()), 0);
  for (const auto& suffix : {"/unsafe", "/link/child"}) {
    EnvironmentDirectories paths = {{"old", "/old"}};
    const EnvironmentDirectories input = {
        {"work", root_ + "/missing/work"}, {"zbad", root_ + suffix}};
    EXPECT_FALSE(AuditEnvironmentPaths(root_ + "/mb.toml", root_, input,
                                      &paths, &error_));
    const auto audit_error = error_;
    EXPECT_TRUE(paths.empty());
    EXPECT_FALSE(Prepare(input));
    EXPECT_EQ(audit_error.code, error_.code);
    EXPECT_EQ(audit_error.key, error_.key);
    EXPECT_EQ(audit_error.system_errno, error_.system_errno);
    EXPECT_FALSE(std::filesystem::exists(root_ + "/missing"));
  }
  EXPECT_EQ(Mode(root_ + "/unsafe"), 0755u);
}

TEST_F(EnvironmentPreparation, InvalidUnselectedEntryPreventsAnyCreation) {
  EXPECT_FALSE(Prepare({{"work", root_ + "/new/work"}, {"zbad", "../bad"}}));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/new"));
  EXPECT_EQ(error_.key, "environments.zbad.data_directory");
  EXPECT_FALSE(Prepare({{"work", root_ + "/new/work"},
                       {"nested", root_ + "/new/work/child"}}));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/new"));
}

TEST_F(EnvironmentPreparation, ExcessiveSelectedPathLengthPreventsAnyCreation) {
  const std::string missing = root_ + "/missing";
  const std::string too_deep = missing + AbsolutePathOfLength(PATH_MAX);
  const std::string oversized_component = missing + "/" + std::string(NAME_MAX + 1, 'a');
  for (const auto& raw : {too_deep, oversized_component}) {
    EXPECT_FALSE(Prepare({{"work", raw}}));
    EXPECT_EQ(error_.code, EnvironmentPathErrorCode::kInvalidPath);
    EXPECT_EQ(error_.key, "environments.work.data_directory");
    EXPECT_FALSE(std::filesystem::exists(missing));
  }
}

TEST_F(EnvironmentPreparation, UnknownSelectionDoesNotCreateAnything) {
  EXPECT_FALSE(Prepare({{"work", root_ + "/new/work"}}, "missing"));
  EXPECT_EQ(error_.code, EnvironmentPathErrorCode::kUnknownEnvironment);
  EXPECT_FALSE(std::filesystem::exists(root_ + "/new"));
}

TEST_F(EnvironmentPreparation, RefusesFinalAndAncestorSymlinksIncludingDanglingLinks) {
  ASSERT_EQ(mkdir((root_ + "/real").c_str(), 0700), 0);
  ASSERT_EQ(symlink((root_ + "/real").c_str(), (root_ + "/link").c_str()), 0);
  EXPECT_FALSE(Prepare({{"work", root_ + "/link"}}));
  EXPECT_FALSE(Prepare({{"work", root_ + "/link/child"}}));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/real/child"));
  ASSERT_EQ(symlink((root_ + "/absent").c_str(), (root_ + "/dangling").c_str()), 0);
  EXPECT_FALSE(Prepare({{"work", root_ + "/dangling/child"}}));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/absent"));
}

TEST_F(EnvironmentPreparation, InvalidUnselectedFilesystemPreventsCreation) {
  ASSERT_EQ(symlink(root_.c_str(), (root_ + "/alias").c_str()), 0);
  EXPECT_FALSE(Prepare({{"work", root_ + "/new/work"},
                       {"zbad", root_ + "/alias/other"}}));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/new"));
  EXPECT_EQ(error_.key, "environments.zbad.data_directory");
}

TEST_F(EnvironmentPreparation, RefusesUnsafeRootWithoutChangingPermissions) {
  ASSERT_EQ(mkdir((root_ + "/public").c_str(), 0755), 0);
  ASSERT_EQ(chmod((root_ + "/public").c_str(), 0755), 0);
  EXPECT_FALSE(Prepare({{"work", root_ + "/public"}}));
  EXPECT_EQ(Mode(root_ + "/public"), 0755u);
  EXPECT_EQ(error_.code, EnvironmentPathErrorCode::kUnsafeFilesystem);
}

TEST_F(EnvironmentPreparation, RefusesWritableAncestorWithoutChmod) {
  ASSERT_EQ(mkdir((root_ + "/public").c_str(), 0777), 0);
  ASSERT_EQ(chmod((root_ + "/public").c_str(), 0777), 0);
  EXPECT_FALSE(Prepare({{"work", root_ + "/public/work"}}));
  EXPECT_FALSE(std::filesystem::exists(root_ + "/public/work"));
  EXPECT_EQ(Mode(root_ + "/public"), 0777u);
}

TEST_F(EnvironmentPreparation, RefusesRootOwnedByAnotherUser) {
  if (geteuid() == 0) GTEST_SKIP() << "Requires non-root test user";
  EXPECT_FALSE(Prepare({{"work", "/etc"}}));
  EXPECT_EQ(error_.code, EnvironmentPathErrorCode::kUnsafeFilesystem);
}

TEST_F(EnvironmentPreparation, RefusesRegularFileInPath) {
  int fd = open((root_ + "/file").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  ASSERT_GE(fd, 0);
  close(fd);
  EXPECT_FALSE(Prepare({{"work", root_ + "/file"}}));
  EXPECT_FALSE(Prepare({{"work", root_ + "/file/work"}}));
  EXPECT_EQ(error_.code, EnvironmentPathErrorCode::kUnsafeFilesystem);
}

TEST_F(EnvironmentPreparation, ConcurrentPreparationConvergesOnOneDirectory) {
  constexpr int count = 12;
  std::atomic<int> ready = 0;
  std::atomic<bool> start = false;
  std::atomic<int> successes = 0;
  std::array<ino_t, count> identities = {};
  std::vector<std::thread> threads;
  for (int i = 0; i < count; ++i) threads.emplace_back([&, i] {
    PreparedEnvironmentRoot prepared;
    EnvironmentPathError error;
    ready.fetch_add(1);
    while (!start.load()) std::this_thread::yield();
    if (PrepareEnvironmentRoot(root_ + "/mb.toml", root_,
        {{"work", root_ + "/parallel/parents/work"}}, "work", &prepared, &error)) {
      struct stat st = {};
      if (fstat(prepared.directory_fd(), &st) == 0 && (st.st_mode & 0777) == 0700) {
        identities[i] = st.st_ino;
        successes.fetch_add(1);
      }
    }
  });
  while (ready.load() != count) std::this_thread::yield();
  start.store(true);
  for (auto& thread : threads) thread.join();
  EXPECT_EQ(successes.load(), count);
  for (const auto identity : identities) EXPECT_EQ(identity, identities[0]);
  EXPECT_EQ(Mode(root_ + "/parallel/parents/work"), 0700u);
}

TEST_F(EnvironmentPreparation, HandleMoveAndFailureReleaseDescriptors) {
  ASSERT_TRUE(Prepare({{"work", root_ + "/work"}}));
  int old_fd = prepared_.directory_fd();
  PreparedEnvironmentRoot moved(std::move(prepared_));
  EXPECT_EQ(prepared_.directory_fd(), -1);
  EXPECT_EQ(moved.directory_fd(), old_fd);
  prepared_ = std::move(moved);
  EXPECT_FALSE(Prepare({{"work", "../invalid"}}));
  EXPECT_EQ(prepared_.directory_fd(), -1);
  EXPECT_EQ(fcntl(old_fd, F_GETFD), -1);
}

}  // namespace
}  // namespace mb
