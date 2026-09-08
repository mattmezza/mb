// SPDX-License-Identifier: BSD-3-Clause

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_context_menu/render_view_context_menu_test_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class MbDevToolsBrowserTest : public InProcessBrowserTest {
 protected:
  content::WebContents* ActiveContents(Browser* browser) {
    return browser->tab_strip_model()->GetActiveWebContents();
  }

  void CloseAndExpectDetached(DevToolsWindow* window,
                              content::WebContents* inspected_contents) {
    ASSERT_NE(nullptr, window);
    DevToolsWindowTesting::CloseDevToolsWindowSync(window);
    EXPECT_EQ(nullptr, DevToolsWindow::GetInstanceForInspectedWebContents(
                           inspected_contents));
  }
};

IN_PROC_BROWSER_TEST_F(MbDevToolsBrowserTest,
                       NativeCtrlShiftICommandOpensAndClosesDevTools) {
  // Chromium's accelerator table maps Ctrl+Shift+I to IDC_DEV_TOOLS. Dispatch
  // that native command directly so this remains independent of platform key
  // synthesis while exercising BrowserCommandController's production route.
  content::WebContents* inspected_contents = ActiveContents(browser());
  ASSERT_NE(nullptr, inspected_contents);
  ASSERT_EQ(nullptr, DevToolsWindow::GetInstanceForInspectedWebContents(
                         inspected_contents));
  ASSERT_TRUE(chrome::IsCommandEnabled(browser(), IDC_DEV_TOOLS));

  DevToolsWindowCreationObserver observer;
  ASSERT_TRUE(chrome::ExecuteCommand(browser(), IDC_DEV_TOOLS));
  observer.WaitForLoad();

  DevToolsWindow* window = observer.devtools_window();
  ASSERT_NE(nullptr, window);
  EXPECT_EQ(inspected_contents, window->GetInspectedWebContents());
  EXPECT_EQ(window, DevToolsWindow::GetInstanceForInspectedWebContents(
                        inspected_contents));
  CloseAndExpectDetached(window, inspected_contents);
}

IN_PROC_BROWSER_TEST_F(MbDevToolsBrowserTest,
                       NativeCommandOpensDevToolsForOffTheRecordTab) {
  Browser* otr_browser = CreateIncognitoBrowser();
  ASSERT_NE(nullptr, otr_browser);
  ASSERT_TRUE(otr_browser->GetProfile()->IsOffTheRecord());

  content::WebContents* inspected_contents = ActiveContents(otr_browser);
  ASSERT_NE(nullptr, inspected_contents);
  ASSERT_TRUE(chrome::IsCommandEnabled(otr_browser, IDC_DEV_TOOLS));

  DevToolsWindowCreationObserver observer;
  ASSERT_TRUE(chrome::ExecuteCommand(otr_browser, IDC_DEV_TOOLS));
  observer.WaitForLoad();

  DevToolsWindow* window = observer.devtools_window();
  ASSERT_NE(nullptr, window);
  EXPECT_EQ(inspected_contents, window->GetInspectedWebContents());
  ASSERT_NE(nullptr, window->GetDevToolsWebContents());
  EXPECT_EQ(otr_browser->GetProfile(),
            window->GetDevToolsWebContents()->GetBrowserContext());
  CloseAndExpectDetached(window, inspected_contents);
}

IN_PROC_BROWSER_TEST_F(MbDevToolsBrowserTest,
                       TestingHelperCreatesDockedAndUndockedWindows) {
  content::WebContents* inspected_contents = ActiveContents(browser());
  ASSERT_NE(nullptr, inspected_contents);

  DevToolsWindow* docked =
      DevToolsWindowTesting::OpenDevToolsWindowSync(inspected_contents, true);
  ASSERT_NE(nullptr, docked);
  EXPECT_TRUE(docked->IsDocked());
  CloseAndExpectDetached(docked, inspected_contents);

  DevToolsWindow* undocked =
      DevToolsWindowTesting::OpenDevToolsWindowSync(inspected_contents, false);
  ASSERT_NE(nullptr, undocked);
  EXPECT_FALSE(undocked->IsDocked());
  CloseAndExpectDetached(undocked, inspected_contents);
}

IN_PROC_BROWSER_TEST_F(MbDevToolsBrowserTest,
                       ContextMenuInspectCommandOpensDevTools) {
  content::WebContents* inspected_contents = ActiveContents(browser());
  ASSERT_NE(nullptr, inspected_contents);

  // This test uses the real context-menu command implementation. Its
  // IDC_CONTENT_CONTEXT_INSPECTELEMENT branch calls ExecInspectElement(),
  // which resolves the frame and calls DevToolsWindow::InspectElement().
  auto menu = TestRenderViewContextMenu::Create(
      inspected_contents, inspected_contents->GetLastCommittedURL());
  ASSERT_NE(nullptr, menu);
  ASSERT_TRUE(menu->IsItemPresent(IDC_CONTENT_CONTEXT_INSPECTELEMENT));
  ASSERT_TRUE(menu->IsItemEnabled(IDC_CONTENT_CONTEXT_INSPECTELEMENT));

  DevToolsWindowCreationObserver observer;
  menu->ExecuteCommand(IDC_CONTENT_CONTEXT_INSPECTELEMENT,
                       /*event_flags=*/0);
  observer.WaitForLoad();

  DevToolsWindow* window = observer.devtools_window();
  ASSERT_NE(nullptr, window);
  EXPECT_EQ(inspected_contents, window->GetInspectedWebContents());
  CloseAndExpectDetached(window, inspected_contents);
}

}  // namespace
