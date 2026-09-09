// SPDX-License-Identifier: BSD-3-Clause
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/test/run_until.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/tab_close_button.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
namespace {
constexpr char kConfiguredOne[] = "about:blank#configured-one";
constexpr char kConfiguredTwo[] = "about:blank#configured-two";
constexpr char kExplicit[] = "data:text/html,Explicit";
constexpr char kRestored[] = "about:blank#restored";
TabView* FindActiveTab(views::View* view) {
  if (auto* tab = views::AsViewClass<TabView>(view); tab && tab->IsActive()) {
    return tab;
  }
  for (views::View* child : view->children()) {
    if (auto* found = FindActiveTab(child)) {
      return found;
    }
  }
  return nullptr;
}
TabCloseButton* FindCloseButton(views::View* view) {
  if (auto* button = views::AsViewClass<TabCloseButton>(view)) {
    return button;
  }
  for (views::View* child : view->children()) {
    if (auto* found = FindCloseButton(child)) {
      return found;
    }
  }
  return nullptr;
}
std::vector<Browser*> ProfileWindows(Profile* profile) {
  std::vector<Browser*> windows;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* window) {
        if (window->GetProfile() == profile &&
            window->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
          windows.push_back(window->GetBrowserForMigrationOnly());
        }
        return true;
      });
  return windows;
}
class MbRuntimeUiActionsBrowserTest : public InProcessBrowserTest {
 public:
  MbRuntimeUiActionsBrowserTest() {
    set_open_about_blank_on_browser_launch(false);
  }

 protected:
  bool SetUpUserDataDirectory() override {
    auto* command_line = base::CommandLine::ForCurrentProcess();
    const auto root = command_line->GetSwitchValuePath("user-data-dir");
    if (!root.IsAbsolute()) {
      return false;
    }
    const std::string name =
        testing::UnitTest::GetInstance()->current_test_info()->name();
    const bool show_close = name == "EnabledCloseButtonRetainsNativePinRule";
    const auto file = root.AppendASCII("mb-actions-config.toml");
    if (!base::WriteFile(
            file,
            std::string("schema_version = 1\n[ui]\nsidebar_width = 300\n") +
                "sidebar_collapsed = false\nshow_tab_close_buttons = " +
                (show_close ? "true" : "false") +
                "\n[environments.personal]\ndata_directory = '.'\n" +
                "startup_urls = ['about:blank#configured-one', "
                "'about:blank#configured-two']\n")) {
      return false;
    }
    command_line->AppendSwitchPath("config", file);
    if (name == "ExplicitUrlsWin") {
      command_line->AppendArg(kExplicit);
    }
    if (name == "IncognitoDoesNotOpenConfiguredUrls") {
      command_line->AppendSwitch("incognito");
    }
    return InProcessBrowserTest::SetUpUserDataDirectory();
  }
  void ExpectConfiguredUrls() {
    auto* tabs = browser()->tab_strip_model();
    ASSERT_EQ(tabs->count(), 2);
    for (int index = 0; index < 2; ++index) {
      ASSERT_TRUE(content::WaitForLoadStop(tabs->GetWebContentsAt(index)));
      EXPECT_EQ(tabs->GetWebContentsAt(index)->GetLastCommittedURL(),
                GURL(index == 0 ? kConfiguredOne : kConfiguredTwo));
    }
    EXPECT_TRUE(browser()
                    ->GetProfile()
                    ->GetPrefs()
                    ->GetList(prefs::kURLsToRestoreOnStartup)
                    .empty());
    EXPECT_TRUE(base::CommandLine::ForCurrentProcess()->GetArgs().empty());
  }
  void ExpectCloseVisibility(bool expected) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      RunScheduledLayouts();
      auto* active = FindActiveTab(&browser()->GetBrowserView());
      auto* close = active ? FindCloseButton(active) : nullptr;
      return close && close->GetVisible() == expected;
    }));
  }
};
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest,
                       ConfiguredUrlsAndHiddenCloseButton) {
  ExpectConfiguredUrls();
  browser()->tab_strip_model()->ActivateTabAt(0);
  ExpectCloseVisibility(false);
  chrome::CloseTab(browser());
  ASSERT_EQ(browser()->tab_strip_model()->count(), 1);
  EXPECT_EQ(browser()
                ->tab_strip_model()
                ->GetActiveWebContents()
                ->GetLastCommittedURL(),
            GURL(kConfiguredTwo));
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest,
                       EnabledCloseButtonRetainsNativePinRule) {
  ExpectConfiguredUrls();
  ExpectCloseVisibility(true);
  browser()->tab_strip_model()->SetTabPinned(
      browser()->tab_strip_model()->active_index(), true);
  ExpectCloseVisibility(false);
  browser()->tab_strip_model()->SetTabPinned(
      browser()->tab_strip_model()->active_index(), false);
  ExpectCloseVisibility(true);
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest, ExplicitUrlsWin) {
  ASSERT_EQ(browser()->tab_strip_model()->count(), 1);
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(contents->GetLastCommittedURL(), GURL(kExplicit));
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest,
                       IncognitoDoesNotOpenConfiguredUrls) {
  EXPECT_TRUE(browser()->GetProfile()->IsOffTheRecord());
  ASSERT_EQ(browser()->tab_strip_model()->count(), 1);
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_NE(contents->GetLastCommittedURL(), GURL(kConfiguredOne));
  EXPECT_NE(contents->GetLastCommittedURL(), GURL(kConfiguredTwo));
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest,
                       PRE_NativeSessionRestoreWins) {
  ExpectConfiguredUrls();
  browser()->tab_strip_model()->ActivateTabAt(0);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL(kRestored)));
  browser()->tab_strip_model()->CloseWebContentsAt(1, 0);
  ASSERT_EQ(browser()->tab_strip_model()->count(), 1);
  SessionStartupPref::SetStartupPref(
      browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest,
                       NativeSessionRestoreWins) {
  EXPECT_TRUE(browser()->CreatedBySessionRestore());
  ASSERT_EQ(browser()->tab_strip_model()->count(), 1);
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(contents->GetLastCommittedURL(), GURL(kRestored));
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiActionsBrowserTest,
                       ExistingProcessDoesNotReopenConfiguredUrls) {
  ExpectConfiguredUrls();
  auto* profile = browser()->GetProfile();
  ASSERT_EQ(ProfileWindows(profile).size(), 1u);
  base::CommandLine incoming(base::CommandLine::NO_PROGRAM);
  StartupBrowserCreator::ProcessCommandLineAlreadyRunning(
      incoming, profile->GetPath(),
      {profile->GetPath(), StartupProfileMode::kBrowserWindow});
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return ProfileWindows(profile).size() == 2u; }));
  for (Browser* window : ProfileWindows(profile)) {
    if (window == browser()) {
      continue;
    }
    ASSERT_EQ(window->tab_strip_model()->count(), 1);
    auto* contents = window->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    EXPECT_NE(contents->GetLastCommittedURL(), GURL(kConfiguredOne));
    EXPECT_NE(contents->GetLastCommittedURL(), GURL(kConfiguredTwo));
  }
  EXPECT_EQ(browser()->tab_strip_model()->count(), 2);
}
}  // namespace
