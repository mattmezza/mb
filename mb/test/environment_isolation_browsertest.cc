// SPDX-License-Identifier: BSD-3-Clause
#include <string>
#include <utility>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/bind.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/history_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/test/extension_background_page_waiter.h"
#include "extensions/test/extension_test_message_listener.h"
#include "mb/browser/browser_config_gate.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "url/gurl.h"

namespace extensions {
namespace {
class MbEnvironmentIsolationBrowserTest : public ExtensionBrowserTest {
protected:
  void SetUpCommandLine(base::CommandLine *command) override {
    ExtensionBrowserTest::SetUpCommandLine(command);
    fixture_root_ = command->GetSwitchValuePath("user-data-dir");
    ASSERT_TRUE(fixture_root_.IsAbsolute());
    const std::string name =
        testing::UnitTest::GetInstance()->current_test_info()->name();
    selected_ = name.starts_with("PRE_") && !name.starts_with("PRE_PRE_")
                    ? "work"
                    : "personal";
    command->RemoveSwitch("user-data-dir");
    command->AppendSwitchPath("user-data-dir",
                              fixture_root_.AppendASCII(selected_));
    command->AppendSwitchASCII("environment", selected_);
  }

  bool SetUpUserDataDirectory() override {
    const auto config = fixture_root_.AppendASCII("environment-test.toml");
    if (!base::WriteFile(
            config, "schema_version = "
                    "1\n[environments.personal]\ndata_directory = 'personal'\n"
                    "[environments.work]\ndata_directory = 'work'\n"))
      return false;
    base::CommandLine::ForCurrentProcess()->AppendSwitchPath("config", config);
    return ExtensionBrowserTest::SetUpUserDataDirectory();
  }

  void SetUpOnMainThread() override {
    ExtensionBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(mb::GetProcessRuntimeConfig());
    EXPECT_EQ(mb::GetProcessRuntimeConfig()->selected_environment, selected_);
    EXPECT_EQ(profile()->GetPath().DirName(),
              fixture_root_.AppendASCII(selected_));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  GURL CookieURL() { return GURL("http://127.0.0.1/"); }
  std::string Read(const char *name) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string value;
    EXPECT_TRUE(
        base::ReadFileToString(fixture_root_.AppendASCII(name), &value));
    return value;
  }
  bool Write(const char *name, const std::string &value) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return base::WriteFile(fixture_root_.AppendASCII(name), value);
  }
  bool HasHistory(const GURL &url) {
    auto *service = HistoryServiceFactory::GetForProfile(
        profile(), ServiceAccessType::EXPLICIT_ACCESS);
    EXPECT_TRUE(service);
    ui_test_utils::WaitForHistoryToLoad(service);
    WaitForHistoryBackendToRun(profile());
    history::QueryURLAndVisitsResult found;
    base::RunLoop loop;
    base::CancelableTaskTracker tracker;
    service->QueryURLAndVisits(
        url, history::VisitQuery404sPolicy::kInclude404s,
        base::BindLambdaForTesting(
            [&](history::QueryURLAndVisitsResult result) {
              found = std::move(result);
              loop.Quit();
            }),
        &tracker);
    loop.Run();
    return found.success && !found.visits.empty();
  }
  const Extension *LoadFixture() {
    return LoadExtension(
        base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
            .AppendASCII("mb/test/extensions/smoke"));
  }
  void VisitAndCount(const Extension &extension, int count,
                     const char *saved_url) {
    ExtensionBackgroundPageWaiter(profile(), extension)
        .WaitForBackgroundInitialized();
    ExtensionTestMessageListener listener("content:" + std::to_string(count));
    listener.set_extension_id(extension.id());
    const GURL url =
        embedded_test_server()->GetURL("/title1.html?environment=" + selected_);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    ASSERT_TRUE(listener.WaitUntilSatisfied());
    EXPECT_TRUE(HasHistory(url));
    if (saved_url)
      EXPECT_TRUE(Write(saved_url, url.spec()));
  }
  base::FilePath fixture_root_;
  std::string selected_;
};

IN_PROC_BROWSER_TEST_F(MbEnvironmentIsolationBrowserTest,
                       PRE_PRE_PersistentRootsRemainSeparate) {
  ASSERT_EQ(content::GetCookies(profile(), CookieURL()), "");
  const Extension *extension = LoadFixture();
  ASSERT_TRUE(extension);
  ASSERT_TRUE(Write("extension-id", extension->id()));
  VisitAndCount(*extension, 1, "personal-url");
  ASSERT_TRUE(
      content::SetCookie(profile(), CookieURL(),
                         "personal=1; Path=/; Max-Age=86400; SameSite=Lax"));
}

IN_PROC_BROWSER_TEST_F(MbEnvironmentIsolationBrowserTest,
                       PRE_PersistentRootsRemainSeparate) {
  EXPECT_EQ(content::GetCookies(profile(), CookieURL()), "");
  EXPECT_FALSE(HasHistory(GURL(Read("personal-url"))));
  EXPECT_FALSE(
      extension_registry()->GetInstalledExtension(Read("extension-id")));
  const Extension *extension = LoadFixture();
  ASSERT_TRUE(extension);
  EXPECT_EQ(extension->id(), Read("extension-id"));
  VisitAndCount(*extension, 1, "work-url");
  ASSERT_TRUE(content::SetCookie(
      profile(), CookieURL(), "work=1; Path=/; Max-Age=86400; SameSite=Lax"));
}

IN_PROC_BROWSER_TEST_F(MbEnvironmentIsolationBrowserTest,
                       PersistentRootsRemainSeparate) {
  EXPECT_EQ(content::GetCookies(profile(), CookieURL()), "personal=1");
  EXPECT_TRUE(HasHistory(GURL(Read("personal-url"))));
  EXPECT_FALSE(HasHistory(GURL(Read("work-url"))));
  const Extension *extension =
      extension_registry()->GetInstalledExtension(Read("extension-id"));
  ASSERT_TRUE(extension);
  // Personal resumes at one stored visit; work's separate visit did not add to
  // it.
  VisitAndCount(*extension, 2, nullptr);
}
} // namespace
} // namespace extensions
