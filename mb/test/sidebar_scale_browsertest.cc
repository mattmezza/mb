// SPDX-License-Identifier: BSD-3-Clause

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/test/gtest_tags.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/test/tracing/test_trace_processor.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/test/vertical_tabs_browser_test_mixin.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/viz/common/frame_timing_details.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mb/ui/sidebar/sidebar_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/perfetto/protos/perfetto/config/trace_config.gen.h"
#include "ui/compositor/compositor.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
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

  struct ProjectionTiming {
    base::TimeTicks start;
    base::TimeTicks first_predicate;
    base::TimeTicks end;
    base::TimeDelta layout_elapsed;
    int layout_calls = 0;
  };

  bool WaitForProjection(int expected_tab_count,
                         ProjectionTiming* timing = nullptr) {
    ProjectionTiming measured;
    measured.start = base::TimeTicks::Now();
    // Deliberately retain the original RunUntil behavior: its first predicate
    // runs at UI-thread idle. The existing total tags remain comparable.
    const bool ready = base::test::RunUntil([&]() {
      const base::TimeTicks layout_start = base::TimeTicks::Now();
      if (measured.first_predicate.is_null()) {
        measured.first_predicate = layout_start;
      }
      RunScheduledLayouts();
      measured.layout_elapsed += base::TimeTicks::Now() - layout_start;
      ++measured.layout_calls;

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
    measured.end = base::TimeTicks::Now();
    if (timing) {
      *timing = measured;
    }
    return ready;
  }

  void RecordNumber(const std::string& name, int64_t value) {
    base::AddTagToTestResult(name, base::NumberToString(value));
  }

  void RecordProjectionTiming(const std::string& prefix,
                              base::TimeTicks operation_start,
                              base::TimeTicks operation_end,
                              const ProjectionTiming& timing) {
    RecordNumber(prefix + "_op_sync_us",
                 (operation_end - operation_start).InMicroseconds());
    RecordNumber(prefix + "_after_op_to_first_idle_us",
                 (timing.first_predicate - operation_end).InMicroseconds());
    RecordNumber(prefix + "_projection_first_idle_delay_us",
                 (timing.first_predicate - timing.start).InMicroseconds());
    RecordNumber(prefix + "_projection_wait_us",
                 (timing.end - timing.start).InMicroseconds());
    RecordNumber(prefix + "_projection_layout_sync_us",
                 timing.layout_elapsed.InMicroseconds());
    // Counts calls to the all-widget helper, not dirty roots or layout passes.
    RecordNumber(prefix + "_projection_layout_calls", timing.layout_calls);
    RecordNumber(
        prefix + "_projection_outside_layout_us",
        (timing.end - timing.start - timing.layout_elapsed).InMicroseconds());
  }

  bool ActiveTabIsVisible(int expected_index) {
    auto* region = region_view();
    if (!region || tab_strip_model()->active_index() != expected_index) {
      return false;
    }
    auto* tab =
        views::AsViewClass<TabView>(region->GetTabAnchorViewAt(expected_index));
    return tab && tab->IsActive() && !tab->GetLocalBounds().IsEmpty() &&
           tab->GetVisibleBounds().Contains(tab->GetLocalBounds());
  }

  void MeasureLoadedSelectionPresentation(int index) {
    const std::string prefix =
        "sidebar_scale_100_visual_select_" + base::NumberToString(index);
    const base::TimeTicks start = base::TimeTicks::Now();
    tab_strip_model()->ActivateTabAt(index);
    const base::TimeTicks operation_end = base::TimeTicks::Now();
    // This separate measurement observes native layout/scroll scheduling. It
    // neither forces layouts nor waits for idle if the visible state is ready.
    if (!ActiveTabIsVisible(index)) {
      ASSERT_TRUE(
          base::test::RunUntil([&] { return ActiveTabIsVisible(index); }));
    }
    const base::TimeTicks ready = base::TimeTicks::Now();

    ui::Compositor* const compositor =
        browser()->GetBrowserView().GetWidget()->GetCompositor();
    ASSERT_NE(compositor, nullptr);
    // TestFuture copies the feedback and weakly binds its callback, so a
    // timeout cannot leave references to this stack in the compositor.
    base::test::TestFuture<const viz::FrameTimingDetails&> presented;
    compositor->RequestSuccessfulPresentationTimeForNextFrame(
        presented.GetCallback());
    // Require another frame after observing the desired state. This is an
    // upper-bound checkpoint, not proof of the earliest frame or input latency.
    region_view()->SchedulePaint();
    compositor->ScheduleDraw();
    ASSERT_TRUE(presented.Wait());
    const base::TimeTicks callback_time = base::TimeTicks::Now();
    const auto& feedback = presented.Get().presentation_feedback;
    const base::TimeTicks presentation_time = feedback.timestamp;
    ASSERT_FALSE(presentation_time.is_null());
    ASSERT_GE(presentation_time, ready);
    ASSERT_TRUE(ActiveTabIsVisible(index));
    RecordNumber(prefix + "_op_sync_us",
                 (operation_end - start).InMicroseconds());
    RecordNumber(prefix + "_native_visible_ready_us",
                 (ready - start).InMicroseconds());
    RecordNumber(prefix + "_next_presented_frame_us",
                 (presentation_time - start).InMicroseconds());
    RecordNumber(prefix + "_presentation_callback_us",
                 (callback_time - start).InMicroseconds());
    RecordNumber(prefix + "_presentation_flags", feedback.flags);
  }

  void TraceLoadedSelections() {
    // Independent third pass. Startup and stop/SQL parsing are outside the
    // traced operations and outside every previously recorded latency metric.
    base::test::TestTraceProcessor trace;
    auto config = base::test::DefaultTraceConfig("ui,views", false);
    trace.StartTrace(config, perfetto::kCustomBackend);
    constexpr std::array kIndexes{0, 99, 50};
    std::array<int64_t, 3> sync_us{};
    for (size_t i = 0; i < kIndexes.size(); ++i) {
      // A fixed marker scopes existing native nested trace events. No URLs or
      // titles are passed as trace arguments or result tags.
      TRACE_EVENT("ui", "MbSidebar.LoadedSelection");
      const base::TimeTicks start = base::TimeTicks::Now();
      tab_strip_model()->ActivateTabAt(kIndexes[i]);
      sync_us[i] = (base::TimeTicks::Now() - start).InMicroseconds();
    }
    const auto status = trace.StopAndParseTrace();
    ASSERT_TRUE(status.ok()) << status.message();

    // Dropped packets/slices make attribution misleading: fail instead of
    // interpreting missing spans as zero cost.
    auto loss = trace.RunQuery(
        "SELECT COALESCE(SUM(value),0) FROM stats "
        "WHERE severity IN ('data_loss','error')");
    ASSERT_TRUE(loss.has_value()) << loss.error();
    ASSERT_EQ(loss->size(), 2u);
    ASSERT_EQ((*loss)[1].size(), 1u);
    int64_t loss_count = 0;
    ASSERT_TRUE(base::StringToInt64((*loss)[1][0], &loss_count));
    RecordNumber("sidebar_trace_data_loss_or_error", loss_count);
    ASSERT_EQ(loss_count, 0);

    auto roots = trace.RunQuery(
        "SELECT id,dur FROM slice "
        "WHERE name='MbSidebar.LoadedSelection' ORDER BY ts,id");
    ASSERT_TRUE(roots.has_value()) << roots.error();
    ASSERT_EQ(roots->size(), kIndexes.size() + 1);

    constexpr std::array<std::pair<const char*, const char*>, 14> kScopes{{
        {"MbSidebar.LoadedSelection", "marker"},
        {"TabStripModel::ActivateTab", "model_activate"},
        {"BrowserView::OnActiveTabChanged", "browser_active"},
        {"BrowserView::UpdateUIForContents", "browser_update_ui"},
        {"BrowserView::Layout", "browser_layout"},
        {"WebView::SetWebContents", "webview_set"},
        {"WebView::AttachWebContentsNativeView", "webview_attach"},
        {"WebView::DetachWebContentsNativeView", "webview_detach"},
        {"Widget::LayoutRootViewIfNecessary", "widget_layout"},
        {"View::LayoutImmediately", "view_layout"},
        {"LayoutManagerBase::Layout", "layout_manager"},
        {"View::LayoutChildren", "view_layout_children"},
        {"View::Layout(bounds_changed)", "view_layout_bounds_changed"},
        {"View::UpdateParentLayers", "view_update_parent_layers"},
    }};

    for (size_t i = 0; i < kIndexes.size(); ++i) {
      ASSERT_EQ((*roots)[i + 1].size(), 2u);
      int64_t root_id = 0;
      int64_t root_duration = 0;
      ASSERT_TRUE(base::StringToInt64((*roots)[i + 1][0], &root_id));
      ASSERT_TRUE(base::StringToInt64((*roots)[i + 1][1], &root_duration));
      ASSERT_GE(root_id, 0);
      ASSERT_GT(root_duration, 0);
      const std::string prefix =
          "sidebar_trace_loaded_select_" + base::NumberToString(kIndexes[i]);
      RecordNumber(prefix + "_instrumented_op_sync_us", sync_us[i]);
      // parent_id is native slice nesting on the same track. Do not mix in
      // unrelated browser/renderer work merely overlapping in wall time.
      const std::string tree =
          "WITH RECURSIVE subtree AS ("
          "SELECT id,parent_id,name,dur FROM slice WHERE id=" +
          base::NumberToString(root_id) +
          " UNION ALL SELECT s.id,s.parent_id,s.name,s.dur FROM slice s "
          "JOIN subtree p ON s.parent_id=p.id) ";
      auto incomplete =
          trace.RunQuery(tree + "SELECT COUNT(*) FROM subtree WHERE dur<0");
      ASSERT_TRUE(incomplete.has_value()) << incomplete.error();
      ASSERT_EQ(incomplete->size(), 2u);
      ASSERT_EQ((*incomplete)[1].size(), 1u);
      ASSERT_EQ((*incomplete)[1][0], "0");

      auto rows =
          trace.RunQuery(tree +
                         "SELECT s.name,COUNT(*),SUM(s.dur),"
                         "SUM(s.dur-COALESCE((SELECT SUM(c.dur) FROM subtree c "
                         "WHERE c.parent_id=s.id),0)),MAX(s.dur) "
                         "FROM subtree s GROUP BY s.name ORDER BY s.name");
      ASSERT_TRUE(rows.has_value()) << rows.error();
      int64_t accounted_self_ns = 0;
      int64_t other_self_ns = 0;
      bool found_model = false;
      bool found_browser = false;
      for (size_t row = 1; row < rows->size(); ++row) {
        const auto& values = (*rows)[row];
        ASSERT_EQ(values.size(), 5u);
        int64_t count = 0, inclusive_ns = 0, self_ns = 0, max_ns = 0;
        ASSERT_TRUE(base::StringToInt64(values[1], &count));
        ASSERT_TRUE(base::StringToInt64(values[2], &inclusive_ns));
        ASSERT_TRUE(base::StringToInt64(values[3], &self_ns));
        ASSERT_TRUE(base::StringToInt64(values[4], &max_ns));
        ASSERT_GE(self_ns, 0);
        accounted_self_ns += self_ns;
        const char* label = nullptr;
        for (const auto& [event_name, safe_label] : kScopes) {
          if (values[0] == event_name) {
            label = safe_label;
            break;
          }
        }
        found_model |= values[0] == "TabStripModel::ActivateTab";
        found_browser |= values[0] == "BrowserView::OnActiveTabChanged";
        if (label) {
          const std::string scope_prefix = prefix + "_" + label;
          RecordNumber(scope_prefix + "_count", count);
          RecordNumber(scope_prefix + "_inclusive_us", inclusive_ns / 1000);
          RecordNumber(scope_prefix + "_self_us", self_ns / 1000);
          RecordNumber(scope_prefix + "_max_us", max_ns / 1000);
        } else {
          other_self_ns += self_ns;
        }
      }
      ASSERT_TRUE(found_model);
      ASSERT_TRUE(found_browser);
      // Self-times partition the root. Inclusive times overlap and must never
      // be added together to claim a proportion of the operation.
      ASSERT_EQ(accounted_self_ns, root_duration);
      RecordNumber(prefix + "_other_self_us", other_self_ns / 1000);
    }
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
  const base::TimeTicks insertion_end = base::TimeTicks::Now();
  RecordElapsedMicros("sidebar_scale_100_insert_model_us", insertion_start);
  ASSERT_EQ(kScaleTabCount, tabs->count());
  ASSERT_EQ(0, tabs->active_index());

  const base::TimeTicks insertion_layout_start = base::TimeTicks::Now();
  ProjectionTiming insertion_timing;
  ASSERT_TRUE(WaitForProjection(kScaleTabCount, &insertion_timing));
  RecordElapsedMicros("sidebar_scale_100_insert_layout_us",
                      insertion_layout_start);
  RecordElapsedMicros("sidebar_scale_100_insert_total_us", insertion_start);
  RecordProjectionTiming("sidebar_scale_100_insert", insertion_start,
                         insertion_end, insertion_timing);

  content::WebContents* const selected_contents = tabs->GetWebContentsAt(50);
  const base::TimeTicks select_start = base::TimeTicks::Now();
  tabs->ActivateTabAt(50);
  const base::TimeTicks select_end = base::TimeTicks::Now();
  ProjectionTiming select_timing;
  ASSERT_TRUE(WaitForProjection(kScaleTabCount, &select_timing));
  RecordElapsedMicros("sidebar_scale_100_select_layout_us", select_start);
  RecordProjectionTiming("sidebar_scale_100_select", select_start, select_end,
                         select_timing);
  ASSERT_EQ(50, tabs->active_index());
  EXPECT_EQ(selected_contents, tabs->GetActiveWebContents());

  const base::TimeTicks reorder_start = base::TimeTicks::Now();
  const int moved_index = tabs->MoveWebContentsAt(50, 10,
                                                  /*select_after_move=*/true);
  const base::TimeTicks reorder_end = base::TimeTicks::Now();
  ProjectionTiming reorder_timing;
  ASSERT_TRUE(WaitForProjection(kScaleTabCount, &reorder_timing));
  RecordElapsedMicros("sidebar_scale_100_reorder_layout_us", reorder_start);
  RecordProjectionTiming("sidebar_scale_100_reorder", reorder_start,
                         reorder_end, reorder_timing);
  ASSERT_EQ(10, moved_index);
  ASSERT_EQ(moved_index, tabs->active_index());
  ASSERT_EQ(selected_contents, tabs->GetActiveWebContents());

  content::WebContents* const closing_contents = tabs->GetActiveWebContents();
  const base::TimeTicks close_start = base::TimeTicks::Now();
  tabs->CloseWebContentsAt(moved_index, /*close_types=*/0);
  const base::TimeTicks close_end = base::TimeTicks::Now();
  ProjectionTiming close_timing;
  ASSERT_TRUE(WaitForProjection(kScaleTabCount - 1, &close_timing));
  RecordElapsedMicros("sidebar_scale_100_close_layout_us", close_start);
  RecordProjectionTiming("sidebar_scale_100_close", close_start, close_end,
                         close_timing);
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
    const base::TimeTicks operation_end = base::TimeTicks::Now();
    ProjectionTiming timing;
    ASSERT_TRUE(WaitForProjection(kScaleTabCount, &timing));
    const std::string metric = "sidebar_scale_100_loaded_select_" +
                               base::NumberToString(index) + "_layout_us";
    RecordElapsedMicros(metric.c_str(), start);
    RecordProjectionTiming(
        "sidebar_scale_100_loaded_select_" + base::NumberToString(index), start,
        operation_end, timing);
    ASSERT_EQ(index, tabs->active_index());
  }

  // Separate, additional pass: all original cold and loaded measurements above
  // finish before these presentation waits can warm or settle the workload.
  for (const int index : std::array{0, 99, 50}) {
    ASSERT_NO_FATAL_FAILURE(MeasureLoadedSelectionPresentation(index));
  }

  ASSERT_NO_FATAL_FAILURE(TraceLoadedSelections());

  // The active item remains a native view anchor after every model mutation.
  VerticalTabStripRegionView* const region = region_view();
  ASSERT_NE(nullptr, region);
  EXPECT_EQ(region->GetTabAnchorViewAt(tabs->active_index()),
            region->GetDefaultFocusableChild());
}
