// Product privacy regression tests use Chromium's real OTR profile lifecycle.

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/bind.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/history/history_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/profile_destruction_waiter.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/cookies/canonical_cookie.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// Exact local paths, with no script, redirects, cookies, or external resources.
std::unique_ptr<net::test_server::HttpResponse> ServePrivacyFixture(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != "/mb-privacy/normal.html" &&
      request.relative_url != "/mb-privacy/private.html") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content(
      "<!doctype html><meta charset=utf-8><title>Privacy fixture</title>"
      "<p>Local privacy fixture</p>");
  return response;
}

class MbIncognitoPersistenceBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_FALSE(browser()->GetProfile()->IsOffTheRecord());
    ASSERT_FALSE(browser()->GetProfile()->HasPrimaryOTRProfile());
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&ServePrivacyFixture));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  GURL NormalURL() const {
    return embedded_test_server()->GetURL("/mb-privacy/normal.html");
  }

  GURL PrivateURL() const {
    return embedded_test_server()->GetURL("/mb-privacy/private.html");
  }

  history::QueryURLAndVisitsResult QueryHistoryURL(
      history::HistoryService* service,
      const GURL& url) {
    history::QueryURLAndVisitsResult result;
    base::RunLoop run_loop;
    base::CancelableTaskTracker tracker;
    service->QueryURLAndVisits(
        url, history::VisitQuery404sPolicy::kInclude404s,
        base::BindLambdaForTesting(
            [&](history::QueryURLAndVisitsResult query_result) {
              result = std::move(query_result);
              run_loop.Quit();
            }),
        &tracker);
    run_loop.Run();
    return result;
  }
};

// This covers retention across OTR destruction, not a browser process restart.
IN_PROC_BROWSER_TEST_F(MbIncognitoPersistenceBrowserTest,
                       LastPrivateWindowDestroysCookiesButKeepsNormalCookie) {
  Profile* const normal = browser()->GetProfile();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), NormalURL()));
  ASSERT_TRUE(content::SetCookie(
      normal, NormalURL(),
      "mb_persistence=normal; Path=/; Max-Age=86400; SameSite=Lax"));
  const auto normal_cookies = content::GetCanonicalCookies(normal, NormalURL());
  ASSERT_EQ(normal_cookies.size(), 1u);
  EXPECT_TRUE(normal_cookies[0].IsPersistent());

  Browser* const first = CreateIncognitoBrowser(normal);
  Profile* const private_profile = first->GetProfile();
  ASSERT_TRUE(private_profile->IsOffTheRecord());
  ASSERT_EQ(private_profile->GetOriginalProfile(), normal);
  ASSERT_NE(private_profile, normal);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, PrivateURL()));
  EXPECT_EQ(content::GetCookies(private_profile, PrivateURL()), "");
  ASSERT_TRUE(content::SetCookie(
      private_profile, PrivateURL(),
      "mb_persistence=private; Path=/; Max-Age=86400; SameSite=Lax"));
  EXPECT_EQ(content::GetCookies(private_profile, PrivateURL()),
            "mb_persistence=private");
  const auto private_cookies =
      content::GetCanonicalCookies(private_profile, PrivateURL());
  ASSERT_EQ(private_cookies.size(), 1u);
  EXPECT_TRUE(private_cookies[0].IsPersistent());
  EXPECT_EQ(content::GetCookies(normal, NormalURL()), "mb_persistence=normal");

  Browser* const second = CreateIncognitoBrowser(normal);
  ASSERT_EQ(second->GetProfile(), private_profile);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, PrivateURL()));
  CloseBrowserSynchronously(first);
  ASSERT_TRUE(normal->HasPrimaryOTRProfile());
  EXPECT_EQ(content::GetCookies(second->GetProfile(), PrivateURL()),
            "mb_persistence=private");

  // Observe before closing the last OTR window. Browser destruction alone
  // does not provide a usable profile-lifecycle barrier. Do not spin a
  // synchronous browser-close loop first: it could consume the destruction
  // notification before ProfileDestructionWaiter starts waiting.
  ProfileDestructionWaiter destroyed(private_profile);
  CloseBrowserAsynchronously(second);
  destroyed.Wait();
  ASSERT_TRUE(destroyed.destroyed());
  ASSERT_FALSE(normal->HasPrimaryOTRProfile());
  EXPECT_EQ(nullptr, normal->GetPrimaryOTRProfile(/*create_if_needed=*/false));
  // Neither destroyed Browser nor Profile pointers are used again.

  Browser* const recreated = CreateIncognitoBrowser(normal);
  ASSERT_TRUE(recreated->GetProfile()->IsOffTheRecord());
  ASSERT_EQ(recreated->GetProfile()->GetOriginalProfile(), normal);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(recreated, PrivateURL()));
  EXPECT_EQ(content::GetCookies(recreated->GetProfile(), PrivateURL()), "");
  EXPECT_EQ(content::GetCookies(normal, NormalURL()), "mb_persistence=normal");

  ProfileDestructionWaiter recreated_destroyed(recreated->GetProfile());
  CloseBrowserAsynchronously(recreated);
  recreated_destroyed.Wait();
  EXPECT_FALSE(normal->HasPrimaryOTRProfile());
  EXPECT_EQ(nullptr, normal->GetPrimaryOTRProfile(/*create_if_needed=*/false));
}

IN_PROC_BROWSER_TEST_F(MbIncognitoPersistenceBrowserTest,
                       PrivateNavigationDoesNotEnterNormalHistory) {
  Profile* const normal = browser()->GetProfile();
  history::HistoryService* const service = HistoryServiceFactory::GetForProfile(
      normal, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_NE(service, nullptr);
  ui_test_utils::WaitForHistoryToLoad(service);
  EXPECT_FALSE(QueryHistoryURL(service, NormalURL()).success);
  EXPECT_FALSE(QueryHistoryURL(service, PrivateURL()).success);

  Browser* const private_browser = CreateIncognitoBrowser(normal);
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  ASSERT_EQ(private_browser->GetProfile()->GetOriginalProfile(), normal);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_browser, PrivateURL()));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), NormalURL()));

  WaitForHistoryBackendToRun(normal);
  const auto normal_result = QueryHistoryURL(service, NormalURL());
  ASSERT_TRUE(normal_result.success);
  EXPECT_FALSE(normal_result.visits.empty());
  EXPECT_FALSE(QueryHistoryURL(service, PrivateURL()).success);

  ProfileDestructionWaiter destroyed(private_browser->GetProfile());
  CloseBrowserAsynchronously(private_browser);
  destroyed.Wait();
  ASSERT_FALSE(normal->HasPrimaryOTRProfile());
  WaitForHistoryBackendToRun(normal);
  EXPECT_TRUE(QueryHistoryURL(service, NormalURL()).success);
  EXPECT_FALSE(QueryHistoryURL(service, PrivateURL()).success);
}

}  // namespace
