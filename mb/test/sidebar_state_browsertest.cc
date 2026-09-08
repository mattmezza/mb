// SPDX-License-Identifier: BSD-3-Clause

#include <memory>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/common/tab_view.h"
#include "chrome/browser/ui/views/tabs/tab/alert_indicator_button.h"
#include "chrome/browser/ui/views/tabs/tab/tab_icon.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_alert.h"
#include "components/tabs/public/tab_network_state.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/no_renderer_crashes_assertion.h"
#include "mb/ui/sidebar/sidebar_view.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace {

std::unique_ptr<net::test_server::HttpResponse> ServeSidebarFixture(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/mb-sidebar/ready.html" &&
      request.relative_url != "/mb-sidebar/recovered.html" &&
      request.relative_url != "/mb-sidebar/audio.html") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(
      request.relative_url == "/mb-sidebar/audio.html"
          ? "<!doctype html><title>Local audio</title>"
            "<audio id=audio loop src=/pink_noise_140ms.wav></audio>"
          : "<!doctype html><title>Local sidebar fixture</title><p>Ready</p>");
  return response;
}

class MbSidebarStateBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    slow_response_ =
        std::make_unique<net::test_server::ControllableHttpResponse>(
            embedded_test_server(), "/mb-sidebar/slow.html");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServeSidebarFixture));
    // The audio case uses this one pinned upstream WAV. GN data must include
    // it.
    embedded_test_server()->ServeFilesFromSourceDirectory(
        "chrome/test/data/media");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/mb-sidebar/ready.html")));
    ASSERT_NE(Sidebar(), nullptr);
    ASSERT_EQ(browser()->GetBrowserView().tab_strip_view(), Sidebar());
    RunScheduledLayouts();
    ASSERT_NE(ActiveTabView(), nullptr);
  }

  void TearDownOnMainThread() override {
    // ControllableHttpResponse must release its UI-sequence state here.
    slow_response_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  mb::SidebarView* Sidebar() {
    return views::AsViewClass<mb::SidebarView>(
        browser()
            ->GetBrowserView()
            .vertical_tab_strip_region_view_for_testing());
  }

  TabView* ActiveTabView() {
    return views::AsViewClass<TabView>(Sidebar()->GetTabAnchorViewAt(
        browser()->tab_strip_model()->active_index()));
  }

  content::WebContents* Contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  net::test_server::ControllableHttpResponse& slow_response() {
    return *slow_response_;
  }

 private:
  std::unique_ptr<net::test_server::ControllableHttpResponse> slow_response_;
};

IN_PROC_BROWSER_TEST_F(MbSidebarStateBrowserTest,
                       LoadingStateReachesNativeIconAndClearsOnCompletion) {
  TabView* const tab = ActiveTabView();
  auto* const icon =
      views::AsViewClass<TabIcon>(tab->GetViewByElementId(kTabIconElementId));
  ASSERT_NE(icon, nullptr);
  EXPECT_EQ(tab->data().network_state, tabs::TabNetworkState::kNone);
  EXPECT_FALSE(icon->GetShowingLoadingAnimation());

  const GURL url = embedded_test_server()->GetURL("/mb-sidebar/slow.html");
  Contents()->GetController().LoadURLWithParams(
      content::NavigationController::LoadURLParams(url));
  slow_response().WaitForRequest();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab->data().network_state == tabs::TabNetworkState::kWaiting &&
           icon->GetShowingLoadingAnimation();
  }));
  EXPECT_TRUE(Contents()->IsLoading());

  // No Content-Length: the document stays loading until Done closes the stream.
  slow_response().Send(
      net::HTTP_OK, "text/html",
      "<!doctype html><title>Loading fixture</title><p>Partial response");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab->data().network_state == tabs::TabNetworkState::kLoading &&
           icon->GetShowingLoadingAnimation();
  }));
  EXPECT_TRUE(Contents()->IsLoading());
  RunScheduledLayouts();
  EXPECT_TRUE(icon->GetVisible());
  EXPECT_GT(icon->width(), 0);

  slow_response().Send("</p>");
  slow_response().Done();
  ASSERT_TRUE(content::WaitForLoadStop(Contents()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab->data().network_state == tabs::TabNetworkState::kNone &&
           !icon->GetShowingLoadingAnimation();
  }));
  EXPECT_EQ(Contents()->GetLastCommittedURL(), url);
  EXPECT_EQ(ActiveTabView(), tab);
}

IN_PROC_BROWSER_TEST_F(MbSidebarStateBrowserTest,
                       RendererCrashAndRecoveryReachExistingNativeTabView) {
  TabView* const tab = ActiveTabView();
  content::WebContents* const contents = Contents();
  EXPECT_FALSE(contents->IsCrashed());
  EXPECT_FALSE(tab->data().is_crashed);
  {
    content::ScopedAllowRendererCrashes allow_owned_renderer(
        contents->GetPrimaryMainFrame()->GetProcess());
    // Native helper shuts down only this renderer and waits for process exit.
    content::CrashTab(contents);
  }
  ASSERT_TRUE(contents->IsCrashed());
  ASSERT_TRUE(base::test::RunUntil([&] { return tab->data().is_crashed; }));
  EXPECT_EQ(ActiveTabView(), tab);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/mb-sidebar/recovered.html")));
  EXPECT_EQ(Contents(), contents);
  ASSERT_FALSE(contents->IsCrashed());
  ASSERT_TRUE(base::test::RunUntil([&] { return !tab->data().is_crashed; }));
  EXPECT_EQ(ActiveTabView(), tab);
  // Actual sad-icon pixels/animation have no public native getter at this pin.
}

IN_PROC_BROWSER_TEST_F(MbSidebarStateBrowserTest,
                       RealLocalAudioAndNativeMuteReachAlertIndicator) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/mb-sidebar/audio.html")));
  TabView* const tab = ActiveTabView();
  auto* const indicator = views::AsViewClass<AlertIndicatorButton>(
      tab->GetViewByElementId(kTabAlertIndicatorButtonElementId));
  ASSERT_NE(indicator, nullptr);
  content::WebContents* const contents = Contents();
  auto* const audible = RecentlyAudibleHelper::FromWebContents(contents);
  ASSERT_NE(audible, nullptr);
  ASSERT_FALSE(contents->IsAudioMuted());
  ASSERT_FALSE(audible->WasRecentlyAudible());
  EXPECT_FALSE(tab->data().alert_state.has_value());

  base::RunLoop became_audible;
  auto audible_subscription = audible->RegisterRecentlyAudibleChangedCallback(
      base::BindLambdaForTesting([&](bool recently_audible) {
        if (recently_audible) {
          became_audible.Quit();
        }
      }));
  // ExecJs supplies the normal test user gesture and awaits the play promise.
  // No autoplay-policy override, MarkAudible, or fake TabData is used.
  ASSERT_TRUE(
      content::ExecJs(contents, "document.getElementById('audio').play()"));
  if (!audible->WasRecentlyAudible()) {
    became_audible.Run();
  }
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->IsCurrentlyAudible() &&
           tab->data().alert_state == tabs::TabAlert::kAudioPlaying &&
           indicator->alert_state_for_testing() ==
               tabs::TabAlert::kAudioPlaying;
  }));
  RunScheduledLayouts();
  EXPECT_TRUE(indicator->GetVisible());
  EXPECT_GT(indicator->width(), 0);

  ASSERT_TRUE(SetTabAudioMuted(contents, true, TabMutedReason::kAudioIndicator,
                               /*extension_id=*/{}));
  EXPECT_TRUE(contents->IsAudioMuted());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return tab->data().alert_state == tabs::TabAlert::kAudioMuting &&
           indicator->alert_state_for_testing() == tabs::TabAlert::kAudioMuting;
  }));
  EXPECT_EQ(GetTabAudioMutedReason(contents), TabMutedReason::kAudioIndicator);

  ASSERT_TRUE(SetTabAudioMuted(contents, false, TabMutedReason::kAudioIndicator,
                               /*extension_id=*/{}));
  EXPECT_FALSE(contents->IsAudioMuted());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->IsCurrentlyAudible() &&
           tab->data().alert_state == tabs::TabAlert::kAudioPlaying &&
           indicator->alert_state_for_testing() ==
               tabs::TabAlert::kAudioPlaying;
  }));
  ASSERT_TRUE(
      content::ExecJs(contents, "document.getElementById('audio').pause()"));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !contents->IsCurrentlyAudible(); }));
  EXPECT_EQ(ActiveTabView(), tab);
  // The recently-audible indicator intentionally remains briefly after pause.
}

}  // namespace
