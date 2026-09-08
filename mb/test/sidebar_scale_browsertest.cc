// SPDX-License-Identifier: BSD-3-Clause

#include <array>
#include <cstdint>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "base/test/gtest_tags.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mb/ui/sidebar/sidebar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace {

constexpr int kScaleTabCount = 100;

}  // namespace

class MbSidebarScaleBrowserTest
    : public VerticalTabsBrowserTestMixin<InProcessBrowserTest> {
 protected:
  mb::SidebarView* region_view() {
    return views::AsViewClass<mb::SidebarView>(
        browser()
            ->GetBrowserView()
            .vertical_tab_strip_region_view_for_testing());
  }

  bool WaitForProjection(int expected_tab_count) {
    return base::test::RunUntil([&]() {
      RunScheduledLayouts();

      TabStripModel* const tabs = tab_strip_model();
      VerticalTabStripRegionView* const region = region_view();
      if (!region || !region->GetTabStripView() ||
          tabs->count() != expected_tab_count) {
        return false;
      }

      const int active_index = tabs->active_index();
      if (active_index == TabStripModel::kNoTab) {
        return false;
      }

      views::View* const first_anchor = region->GetTabAnchorViewAt(0);
      views::View* const last_anchor =
          region->GetTabAnchorViewAt(expected_tab_count - 1);
      views::View* const active_anchor =
          region->GetTabAnchorViewAt(active_index);
      return first_anchor && last_anchor && active_anchor &&
             !active_anchor->bounds().IsEmpty() &&
             region->GetDefaultFocusableChild() == active_anchor;
    });
  }

  void RecordElapsedMicros(const char* property_name,
                           base::TimeTicks start_time) {
    const int64_t elapsed_micros =
        (base::TimeTicks::Now() - start_time).InMicroseconds();
    // Chromium's custom launcher writer does not serialize gtest
    // RecordProperty. Native result tags survive into the launcher's summary
    // JSON.
    base::AddTagToTestResult(property_name,
                             base::NumberToString(elapsed_micros));
  }
};

IN_PROC_BROWSER_TEST_F(MbSidebarScaleBrowserTest,
                       OneHundredTabsUseNativeModelAndProjection) {
  TabStripModel* const tabs = tab_strip_model();
  ASSERT_EQ(1, tabs->count());

  // Populate through the browser model's delegate. The only page content is
  // local about:blank; no synthetic sidebar or parallel tab list is involved.
  const base::TimeTicks insertion_start = base::TimeTicks::Now();
  for (int index = tabs->count(); index < kScaleTabCount; ++index) {
    tabs->delegate()->AddTabAt(GURL(url::kAboutBlankURL), -1,
                               /*foreground=*/false);
  }
  RecordElapsedMicros("sidebar_scale_100_insert_model_us", insertion_start);
  ASSERT_EQ(kScaleTabCount, tabs->count());
  ASSERT_EQ(0, tabs->active_index());

  const base::TimeTicks insertion_layout_start = base::TimeTicks::Now();
  ASSERT_TRUE(WaitForProjection(kScaleTabCount));
  RecordElapsedMicros("sidebar_scale_100_insert_layout_us",
                      insertion_layout_start);
  RecordElapsedMicros("sidebar_scale_100_insert_total_us", insertion_start);

  content::WebContents* const selected_contents = tabs->GetWebContentsAt(50);
  const base::TimeTicks select_start = base::TimeTicks::Now();
  tabs->ActivateTabAt(50);
  ASSERT_TRUE(WaitForProjection(kScaleTabCount));
  RecordElapsedMicros("sidebar_scale_100_select_layout_us", select_start);
  ASSERT_EQ(50, tabs->active_index());
  EXPECT_EQ(selected_contents, tabs->GetActiveWebContents());

  const base::TimeTicks reorder_start = base::TimeTicks::Now();
  const int moved_index = tabs->MoveWebContentsAt(50, 10,
                                                  /*select_after_move=*/true);
  ASSERT_TRUE(WaitForProjection(kScaleTabCount));
  RecordElapsedMicros("sidebar_scale_100_reorder_layout_us", reorder_start);
  ASSERT_EQ(10, moved_index);
  ASSERT_EQ(moved_index, tabs->active_index());
  ASSERT_EQ(selected_contents, tabs->GetActiveWebContents());

  content::WebContents* const closing_contents = tabs->GetActiveWebContents();
  const base::TimeTicks close_start = base::TimeTicks::Now();
  tabs->CloseWebContentsAt(moved_index, /*close_types=*/0);
  ASSERT_TRUE(WaitForProjection(kScaleTabCount - 1));
  RecordElapsedMicros("sidebar_scale_100_close_layout_us", close_start);
  ASSERT_EQ(kScaleTabCount - 1, tabs->count());
  EXPECT_NE(closing_contents, tabs->GetActiveWebContents());

  // Retain the cold measurements above, then separately measure selection
  // after every local page has finished loading. Restoring the closed tab keeps
  // this comparison at 100 tabs rather than quietly reducing the workload.
  tabs->delegate()->AddTabAt(GURL(url::kAboutBlankURL), -1,
                             /*foreground=*/false);
  ASSERT_EQ(kScaleTabCount, tabs->count());
  for (int index = 0; index < tabs->count(); ++index) {
    SCOPED_TRACE(index);
    ASSERT_TRUE(content::WaitForLoadStop(tabs->GetWebContentsAt(index)));
  }
  ASSERT_TRUE(WaitForProjection(kScaleTabCount));
  for (const int index : std::array{0, 99, 50}) {
    const base::TimeTicks start = base::TimeTicks::Now();
    tabs->ActivateTabAt(index);
    ASSERT_TRUE(WaitForProjection(kScaleTabCount));
    const std::string metric = "sidebar_scale_100_loaded_select_" +
                               base::NumberToString(index) + "_layout_us";
    RecordElapsedMicros(metric.c_str(), start);
    ASSERT_EQ(index, tabs->active_index());
  }

  // The active item remains a native view anchor after every model mutation.
  VerticalTabStripRegionView* const region = region_view();
  ASSERT_NE(nullptr, region);
  EXPECT_EQ(region->GetTabAnchorViewAt(tabs->active_index()),
            region->GetDefaultFocusableChild());
}
