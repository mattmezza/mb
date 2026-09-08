// SPDX-License-Identifier: BSD-3-Clause

#include "mb/browser/browser_window_adapter.h"

#include <optional>

#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mb {
namespace {

class MbBrowserWindowAdapterBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(MbBrowserWindowAdapterBrowserTest,
                       UsesLiveModelAndRejectsStaleHandles) {
  BrowserWindowAdapter window(browser());
  TabCollectionAdapter tabs = window.tabs();

  ASSERT_FALSE(window.is_incognito());
  ASSERT_EQ(1, tabs.Count());
  ASSERT_TRUE(tabs.Active());
  const TabSnapshot first = *tabs.Active();

  ASSERT_TRUE(window.commands().Execute(BrowserCommand::kNewTab));
  ASSERT_EQ(2, tabs.Count());
  ASSERT_TRUE(tabs.Active());
  const TabSnapshot second = *tabs.Active();
  ASSERT_NE(first.handle, second.handle);

  ASSERT_TRUE(tabs.Select(first.handle));
  ASSERT_TRUE(tabs.Active());
  EXPECT_EQ(first.handle, tabs.Active()->handle);
  std::optional<int> moved_index = tabs.Move(second.handle, 0);
  ASSERT_TRUE(moved_index);
  EXPECT_EQ(0, *moved_index);
  ASSERT_TRUE(tabs.At(0));
  EXPECT_EQ(second.handle, tabs.At(0)->handle);
  ASSERT_TRUE(tabs.SetPinned(second.handle, true));
  ASSERT_TRUE(tabs.At(0));
  EXPECT_TRUE(tabs.At(0)->pinned);

  // The native model constrains a pinned tab to the pinned range.
  moved_index = tabs.Move(second.handle, tabs.Count() - 1);
  ASSERT_TRUE(moved_index);
  EXPECT_EQ(0, *moved_index);

  Browser* other_browser = CreateBrowser(browser()->GetProfile());
  BrowserWindowAdapter other_window(other_browser);
  EXPECT_FALSE(other_window.tabs().Select(first.handle));
  EXPECT_FALSE(other_window.tabs().Close(first.handle));
  EXPECT_EQ(1, other_window.tabs().Count());

  ASSERT_TRUE(tabs.Close(second.handle));
  EXPECT_EQ(1, tabs.Count());
  EXPECT_FALSE(tabs.Select(second.handle));
  EXPECT_FALSE(tabs.Close(second.handle));
  EXPECT_FALSE(tabs.Move(second.handle, 0));
  EXPECT_FALSE(tabs.SetPinned(second.handle, true));
}

}  // namespace
}  // namespace mb
