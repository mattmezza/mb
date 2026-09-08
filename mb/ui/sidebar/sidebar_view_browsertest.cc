// Focused product sidebar tests using the authoritative native projection.

#include "mb/ui/sidebar/sidebar_view.h"

#include "base/feature_list.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/tabs/vertical_tab_iph_controller.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/actions/actions.h"
#include "ui/gfx/scoped_animation_duration_scale_mode.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace {

mb::SidebarView* ActiveSidebar(Browser* browser) {
  auto* region =
      browser->GetBrowserView().vertical_tab_strip_region_view_for_testing();
  return views::AsViewClass<mb::SidebarView>(region);
}

tabs::VerticalTabStripStateController* Controller(Browser* browser) {
  return tabs::VerticalTabStripStateController::From(browser);
}

}  // namespace

class MbSidebarBrowserTest : public InProcessBrowserTest {
 public:
  MbSidebarBrowserTest() {
    // Product availability must not depend on native feature trials or a test
    // mixin entering vertical mode. Disable hover expansion to keep geometry
    // checks independent of the pointer position on the test display.
    features_.InitWithFeatures(
        {feature_engagement::kIPHVerticalTabstripTutorialFeature},
        {tabs::kVerticalTabs, tabs::kVerticalTabsLaunch,
         tabs::kVerticalTabsExpandOnHover});
  }

 private:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(MbSidebarBrowserTest,
                       NormalWindowUsesSingleSidebarProjection) {
  EXPECT_FALSE(base::FeatureList::IsEnabled(tabs::kVerticalTabs));
  EXPECT_FALSE(base::FeatureList::IsEnabled(tabs::kVerticalTabsLaunch));
  EXPECT_FALSE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kVerticalTabsEnabled));
  EXPECT_FALSE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kVerticalTabsEnabledFirstTime));
  auto* iph =
      VerticalTabIphController::Get(browser()->GetUnownedUserDataHost());
  ASSERT_NE(iph, nullptr);
  // Fixed vertical windows cannot enter the horizontal-to-vertical tutorial,
  // even when its feature is enabled and native first-use prefs remain false.
  EXPECT_FALSE(TabStripModelObserver::IsObservingAny(iph));
  BrowserView* const browser_view = &browser()->GetBrowserView();
  mb::SidebarView* const sidebar = ActiveSidebar(browser());
  ASSERT_NE(sidebar, nullptr);
  RunScheduledLayouts();
  views::View* const header = sidebar->header_for_testing();
  ASSERT_NE(header, nullptr);
  EXPECT_GT(header->height(), 0);
  // The header must reserve only its content height. An unconstrained flex
  // child previously consumed half the window and pushed tabs down the rail.
  EXPECT_EQ(header->height(), header->GetPreferredSize().height());
  ASSERT_EQ(browser_view->tab_strip_view(), sidebar);
  ASSERT_NE(sidebar->GetTabStripView(), nullptr);
  ASSERT_NE(sidebar->root_node_for_testing(), nullptr);
  views::View* const projection = sidebar->GetTabStripView();
  auto* const root = sidebar->root_node_for_testing();

  TabStripModel* const model = browser()->tab_strip_model();
  ASSERT_EQ(model->count(), 1);
  views::View* const first_tab = sidebar->GetTabAnchorViewAt(0);
  ASSERT_NE(first_tab, nullptr);
  EXPECT_EQ(sidebar->GetDefaultFocusableChild(), first_tab);

  model->delegate()->AddTabAt(GURL("about:blank"), -1, true);
  ASSERT_EQ(model->count(), 2);
  RunScheduledLayouts();
  ASSERT_EQ(browser_view->tab_strip_view(), sidebar);
  EXPECT_EQ(sidebar->GetTabStripView(), projection);
  EXPECT_EQ(sidebar->root_node_for_testing(), root);
  EXPECT_EQ(sidebar->GetTabAnchorViewAt(0), first_tab);
  views::View* const second_tab = sidebar->GetTabAnchorViewAt(1);
  ASSERT_NE(second_tab, nullptr);
  EXPECT_NE(first_tab, second_tab);
  // Foreground insertion must update the native pane's focus target, not just
  // the authoritative model's count.
  EXPECT_EQ(sidebar->GetDefaultFocusableChild(), second_tab);
}

IN_PROC_BROWSER_TEST_F(MbSidebarBrowserTest,
                       OrientationLockKeepsProjectionVertical) {
  tabs::VerticalTabStripStateController* const controller =
      Controller(browser());
  mb::SidebarView* const sidebar = ActiveSidebar(browser());
  ASSERT_NE(controller, nullptr);
  ASSERT_NE(sidebar, nullptr);
  views::View* const tab_projection = sidebar->GetTabStripView();
  ASSERT_NE(tab_projection, nullptr);
  ASSERT_TRUE(controller->IsOrientationFixed());
  ASSERT_TRUE(controller->ShouldDisplayVerticalTabs());

  auto* const toggle_action = actions::ActionManager::Get().FindAction(
      kActionToggleVerticalTabs, browser()->GetActions()->root_action_item());
  ASSERT_NE(toggle_action, nullptr);
  EXPECT_FALSE(toggle_action->GetEnabled());
  EXPECT_FALSE(toggle_action->GetVisible());
  EXPECT_FALSE(chrome::IsCommandEnabled(browser(), IDC_TOGGLE_VERTICAL_TABS));

  TabStripModel* const model = browser()->tab_strip_model();
  model->delegate()->AddTabAt(GURL("about:blank"), -1, true);
  ASSERT_EQ(model->count(), 2);
  model->SelectTabAt(0);
  ASSERT_TRUE(model->IsTabSelected(0));
  ASSERT_TRUE(model->IsTabSelected(1));
  content::WebContents* const active_contents = model->GetActiveWebContents();
  auto expect_unchanged = [&]() {
    RunScheduledLayouts();
    EXPECT_TRUE(controller->ShouldDisplayVerticalTabs());
    EXPECT_EQ(browser()->GetBrowserView().tab_strip_view(), sidebar);
    EXPECT_EQ(sidebar->GetTabStripView(), tab_projection);
    EXPECT_TRUE(model->IsTabSelected(0));
    EXPECT_TRUE(model->IsTabSelected(1));
    EXPECT_EQ(model->GetActiveWebContents(), active_contents);
  };

  auto lock = controller->GetEnableStateLock();
  controller->SetVerticalTabsEnabled(false);
  expect_unchanged();
  chrome::ToggleVerticalTabs(browser());
  expect_unchanged();
  PrefService* const pref_service = browser()->GetProfile()->GetPrefs();
  pref_service->SetBoolean(prefs::kVerticalTabsEnabled, true);
  ASSERT_TRUE(pref_service->GetBoolean(prefs::kVerticalTabsEnabled));
  expect_unchanged();
  pref_service->SetBoolean(prefs::kVerticalTabsEnabled, false);
  ASSERT_FALSE(pref_service->GetBoolean(prefs::kVerticalTabsEnabled));
  expect_unchanged();

  lock.reset();
  expect_unchanged();
  // Exercise preference callbacks again without the native fullscreen lock.
  pref_service->SetBoolean(prefs::kVerticalTabsEnabled, true);
  expect_unchanged();
  pref_service->SetBoolean(prefs::kVerticalTabsEnabled, false);
  expect_unchanged();
}

IN_PROC_BROWSER_TEST_F(MbSidebarBrowserTest,
                       ResizeAndCollapsePreferencesReachSecondWindow) {
  gfx::ScopedAnimationDurationScaleMode disable_animation(
      gfx::ScopedAnimationDurationScaleMode::ZERO_DURATION);
  tabs::VerticalTabStripStateController* const first = Controller(browser());
  mb::SidebarView* const first_sidebar = ActiveSidebar(browser());
  ASSERT_NE(first, nullptr);
  ASSERT_NE(first_sidebar, nullptr);
  first->RequestCollapse(false);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    RunScheduledLayouts();
    return !first->IsCollapsed() &&
           first_sidebar->width() == first->GetUncollapsedWidth();
  }));
  const int requested_width = first->GetUncollapsedWidth() == 275 ? 285 : 275;
  // Exercise the native resize delegate and ensure an actual state change;
  // simply writing the controller's existing width need not update prefs.
  first_sidebar->OnResize(requested_width - first_sidebar->width(),
                          /*done_resizing=*/true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    RunScheduledLayouts();
    return first_sidebar->width() == requested_width;
  }));
  EXPECT_EQ(first->GetUncollapsedWidth(), requested_width);
  EXPECT_EQ(browser()->GetProfile()->GetPrefs()->GetInteger(
                prefs::kVerticalTabsUncollapsedWidth),
            requested_width);
  first->RequestCollapse(true);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    RunScheduledLayouts();
    return first->IsCollapsed() &&
           first_sidebar->width() ==
               VerticalTabStripRegionView::kCollapsedWidth;
  }));
  EXPECT_EQ(browser()->GetProfile()->GetPrefs()->GetInteger(
                prefs::kVerticalTabsUncollapsedWidth),
            requested_width);
  EXPECT_TRUE(browser()->GetProfile()->GetPrefs()->GetBoolean(
      prefs::kVerticalTabsCollapsedState));

  Browser* const second = CreateBrowser(browser()->GetProfile());
  ASSERT_NE(second, nullptr);
  tabs::VerticalTabStripStateController* const second_controller =
      Controller(second);
  ASSERT_NE(second_controller, nullptr);
  mb::SidebarView* const second_sidebar = ActiveSidebar(second);
  ASSERT_NE(second_sidebar, nullptr);
  EXPECT_NE(second_sidebar, first_sidebar);
  RunScheduledLayouts();
  ASSERT_TRUE(base::test::RunUntil([&]() {
    RunScheduledLayouts();
    return second_controller->IsCollapsed() &&
           second_sidebar->width() ==
               VerticalTabStripRegionView::kCollapsedWidth;
  }));
  EXPECT_EQ(second_controller->GetUncollapsedWidth(), requested_width);
  EXPECT_EQ(second->GetBrowserView().tab_strip_view(), second_sidebar);
  second_controller->RequestCollapse(false);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    RunScheduledLayouts();
    return !second_controller->IsCollapsed() &&
           second_sidebar->width() == requested_width;
  }));
  // Width/collapse state is per window; the shared profile supplies defaults
  // for new windows rather than changing another live window's state.
  EXPECT_TRUE(first->IsCollapsed());
  EXPECT_EQ(first_sidebar->width(),
            VerticalTabStripRegionView::kCollapsedWidth);
}
