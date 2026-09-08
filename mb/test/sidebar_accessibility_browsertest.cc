// SPDX-License-Identifier: BSD-3-Clause

#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mb/ui/sidebar/sidebar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_utils.h"
#include "url/gurl.h"

namespace {

mb::SidebarView* ActiveSidebar(Browser* browser) {
  auto* region =
      browser->GetBrowserView().vertical_tab_strip_region_view_for_testing();
  return views::AsViewClass<mb::SidebarView>(region);
}

std::u16string AccessibleName(views::View* view) {
  ui::AXNodeData data;
  view->GetViewAccessibility().GetAccessibleNodeData(&data);
  return data.GetString16Attribute(ax::mojom::StringAttribute::kName);
}

}  // namespace

class MbSidebarAccessibilityBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(MbSidebarAccessibilityBrowserTest,
                       NativeTabAccessibilityTracksModelSelection) {
  mb::SidebarView* const sidebar = ActiveSidebar(browser());
  ASSERT_NE(sidebar, nullptr);
  TabStripModel* const model = browser()->tab_strip_model();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      GURL("data:text/html;base64,PHRpdGxlPlNpZGViYXIgT25lPC90aXRsZT4=")));
  model->delegate()->AddTabAt(
      GURL("data:text/html;base64,PHRpdGxlPlNpZGViYXIgVHdvPC90aXRsZT4="), -1,
      true);
  ASSERT_EQ(model->count(), 2);
  ASSERT_TRUE(content::WaitForLoadStop(model->GetWebContentsAt(1)));

  auto* first = views::AsViewClass<TabView>(sidebar->GetTabAnchorViewAt(0));
  auto* second = views::AsViewClass<TabView>(sidebar->GetTabAnchorViewAt(1));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  ui::AXNodeData first_data;
  ui::AXNodeData second_data;
  first->GetViewAccessibility().GetAccessibleNodeData(&first_data);
  second->GetViewAccessibility().GetAccessibleNodeData(&second_data);
  EXPECT_EQ(ax::mojom::Role::kTab, first_data.role);
  EXPECT_EQ(ax::mojom::Role::kTab, second_data.role);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return AccessibleName(first) == u"Sidebar One" &&
           AccessibleName(second) == u"Sidebar Two";
  }));
  first_data = ui::AXNodeData();
  second_data = ui::AXNodeData();
  first->GetViewAccessibility().GetAccessibleNodeData(&first_data);
  second->GetViewAccessibility().GetAccessibleNodeData(&second_data);
  EXPECT_EQ(u"Sidebar One",
            first_data.GetString16Attribute(ax::mojom::StringAttribute::kName));
  EXPECT_EQ(u"Sidebar Two", second_data.GetString16Attribute(
                                ax::mojom::StringAttribute::kName));
  EXPECT_FALSE(
      first_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_TRUE(
      second_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));

  model->ActivateTabAt(0);
  first_data = ui::AXNodeData();
  second_data = ui::AXNodeData();
  first->GetViewAccessibility().GetAccessibleNodeData(&first_data);
  second->GetViewAccessibility().GetAccessibleNodeData(&second_data);
  EXPECT_TRUE(first_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_FALSE(
      second_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
  EXPECT_EQ(0, model->active_index());
}

IN_PROC_BROWSER_TEST_F(MbSidebarAccessibilityBrowserTest,
                       FocusedNativeTabActivatesThroughReturnKey) {
  mb::SidebarView* const sidebar = ActiveSidebar(browser());
  ASSERT_NE(sidebar, nullptr);
  TabStripModel* const model = browser()->tab_strip_model();
  model->delegate()->AddTabAt(
      GURL("data:text/html;base64,PHRpdGxlPlNpZGViYXIgVHdvPC90aXRsZT4="), -1,
      false);
  ASSERT_EQ(model->count(), 2);
  ASSERT_TRUE(content::WaitForLoadStop(model->GetWebContentsAt(1)));
  model->ActivateTabAt(0);

  auto* second = views::AsViewClass<TabView>(sidebar->GetTabAnchorViewAt(1));
  ASSERT_NE(second, nullptr);
  views::FocusManager* const focus_manager =
      browser()->GetBrowserView().GetFocusManager();
  ASSERT_NE(focus_manager, nullptr);
  focus_manager->SetFocusedView(second);
  ASSERT_TRUE(base::test::RunUntil([&]() { return second->HasFocus(); }));
  EXPECT_EQ(second, focus_manager->GetFocusedView());
  EXPECT_EQ(0, model->active_index());

  ui::test::EventGenerator events(
      views::GetRootWindow(browser()->GetBrowserView().GetWidget()),
      browser()->GetBrowserView().GetNativeWindow());
  events.PressKey(ui::KeyboardCode::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(
      base::test::RunUntil([&]() { return model->active_index() == 1; }));
  // Native BrowserView restores the selected WebContents focus on activation.
  // Return commits the sidebar selection and moves keyboard input to the page.
  ASSERT_NE(focus_manager->GetFocusedView(), nullptr);
  EXPECT_TRUE(browser()->GetBrowserView().GetActiveContentsWebView()->Contains(
      focus_manager->GetFocusedView()));
  ui::AXNodeData second_data;
  second->GetViewAccessibility().GetAccessibleNodeData(&second_data);
  EXPECT_TRUE(
      second_data.GetBoolAttribute(ax::mojom::BoolAttribute::kSelected));
}
