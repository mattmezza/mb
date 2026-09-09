// SPDX-License-Identifier: BSD-3-Clause
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/test/run_until.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
namespace {
class MbRuntimeUiConfigBrowserTest : public InProcessBrowserTest {
 public:
  MbRuntimeUiConfigBrowserTest() {
    set_open_about_blank_on_browser_launch(false);
  }

 protected:
  bool SetUpUserDataDirectory() override {
    auto* command_line = base::CommandLine::ForCurrentProcess();
    const auto root = command_line->GetSwitchValuePath("user-data-dir");
    if (!root.IsAbsolute()) {
      return false;
    }
    const std::string test_name =
        testing::UnitTest::GetInstance()->current_test_info()->name();
    const bool changed =
        test_name == "ChangedConfigOverridesAllRestoredWindows";
    const bool old_collapsed =
        test_name == "PRE_ChangedConfigOverridesAllRestoredWindows";
    const auto file = root.AppendASCII("mb-ui-config.toml");
    const std::string settings =
        changed ? "sidebar_width = 320\nsidebar_collapsed = false\ntheme = "
                  "'light'\n"
                : std::string("sidebar_width = 300\nsidebar_collapsed = ") +
                      (old_collapsed ? "true" : "false") + "\ntheme = 'dark'\n";
    if (!base::WriteFile(
            file, "schema_version = 1\n[ui]\n" + settings +
                      "[environments.personal]\ndata_directory = '.'\n")) {
      return false;
    }
    command_line->AppendSwitchPath("config", file);
    return InProcessBrowserTest::SetUpUserDataDirectory();
  }
  static tabs::VerticalTabStripStateController* Controller(Browser* target) {
    return tabs::VerticalTabStripStateController::From(target);
  }
  static std::vector<Browser*> NormalBrowsers(Profile* profile) {
    std::vector<Browser*> result;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* item) {
          if (item->GetProfile() == profile &&
              item->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
            result.push_back(item->GetBrowserForMigrationOnly());
          }
          return true;
        });
    return result;
  }
  void ChangeNativeState(Browser* target, int width) {
    auto* controller = Controller(target);
    ASSERT_TRUE(controller);
    controller->SetUncollapsedWidth(width);
    controller->RequestCollapse(true);
    ASSERT_TRUE(
        base::test::RunUntil([&] { return controller->IsCollapsed(); }));
    EXPECT_EQ(controller->GetUncollapsedWidth(), width);
  }
  void EnableSessionRestore() {
    SessionStartupPref::SetStartupPref(
        browser()->GetProfile(), SessionStartupPref(SessionStartupPref::LAST));
  }
};
IN_PROC_BROWSER_TEST_F(MbRuntimeUiConfigBrowserTest,
                       PRE_UnchangedConfigPreservesNativeUserState) {
  ASSERT_TRUE(Controller(browser()));
  EXPECT_EQ(Controller(browser())->GetUncollapsedWidth(), 300);
  EXPECT_FALSE(Controller(browser())->IsCollapsed());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(browser()->GetProfile())
                ->GetBrowserColorScheme(),
            ThemeService::BrowserColorScheme::kDark);
  EnableSessionRestore();
  ChangeNativeState(browser(), 350);
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kLight);
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiConfigBrowserTest,
                       UnchangedConfigPreservesNativeUserState) {
  EXPECT_TRUE(browser()->CreatedBySessionRestore());
  ASSERT_TRUE(Controller(browser()));
  EXPECT_EQ(Controller(browser())->GetUncollapsedWidth(), 350);
  EXPECT_TRUE(Controller(browser())->IsCollapsed());
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(browser()->GetProfile())
                ->GetBrowserColorScheme(),
            ThemeService::BrowserColorScheme::kLight);
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiConfigBrowserTest,
                       PRE_ChangedConfigOverridesAllRestoredWindows) {
  EnableSessionRestore();
  ChangeNativeState(browser(), 350);
  Browser* second = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(second);
  ChangeNativeState(second, 270);
  ThemeServiceFactory::GetForProfile(browser()->GetProfile())
      ->SetBrowserColorScheme(ThemeService::BrowserColorScheme::kSystem);
  ASSERT_EQ(NormalBrowsers(browser()->GetProfile()).size(), 2u);
}
IN_PROC_BROWSER_TEST_F(MbRuntimeUiConfigBrowserTest,
                       ChangedConfigOverridesAllRestoredWindows) {
  auto windows = NormalBrowsers(browser()->GetProfile());
  ASSERT_EQ(windows.size(), 2u);
  for (Browser* window : windows) {
    EXPECT_TRUE(window->CreatedBySessionRestore());
    ASSERT_TRUE(Controller(window));
    EXPECT_EQ(Controller(window)->GetUncollapsedWidth(), 320);
    EXPECT_FALSE(Controller(window)->IsCollapsed());
  }
  EXPECT_EQ(ThemeServiceFactory::GetForProfile(browser()->GetProfile())
                ->GetBrowserColorScheme(),
            ThemeService::BrowserColorScheme::kLight);
  ChangeNativeState(browser(), 365);
  Browser* later = CreateBrowser(browser()->GetProfile());
  ASSERT_TRUE(later);
  ASSERT_TRUE(Controller(later));
  EXPECT_EQ(Controller(later)->GetUncollapsedWidth(), 365);
  EXPECT_TRUE(Controller(later)->IsCollapsed());
}
}  // namespace
