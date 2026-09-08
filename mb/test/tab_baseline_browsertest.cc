// Product tab operations use Chromium's authoritative browser model.

#include <string>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

GURL LocalPage(const char* title, const char* body) {
  return GURL(std::string("data:text/html,<title>") + title + "</title><p>" +
              body + "</p>");
}

void ExpectURL(content::WebContents* contents, const GURL& expected) {
  ASSERT_NE(contents, nullptr);
  EXPECT_EQ(contents->GetLastCommittedURL(), expected);
}

}  // namespace

class MbTabBaselineBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(MbTabBaselineBrowserTest,
                       CreateActivateReorderAndClose) {
  TabStripModel* const tabs = browser()->tab_strip_model();
  const GURL first_url = LocalPage("first", "first page");
  const GURL second_url = LocalPage("second", "second page");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  content::WebContents* const first = tabs->GetActiveWebContents();
  ExpectURL(first, first_url);

  // Add through the model's delegate, then use the model as the source of
  // truth for identity, selection, ordering, and close operations.
  tabs->delegate()->AddTabAt(second_url, -1, true);
  ASSERT_EQ(tabs->count(), 2);
  content::WebContents* const second = tabs->GetActiveWebContents();
  ASSERT_NE(second, first);
  ASSERT_TRUE(content::WaitForLoadStop(second));
  ExpectURL(second, second_url);
  ASSERT_EQ(tabs->active_index(), 1);

  tabs->ActivateTabAt(0);
  ASSERT_EQ(tabs->active_index(), 0);
  EXPECT_EQ(tabs->GetWebContentsAt(0), first);

  EXPECT_EQ(tabs->MoveWebContentsAt(0, 1, true), 1);
  ASSERT_EQ(tabs->count(), 2);
  EXPECT_EQ(tabs->GetWebContentsAt(0), second);
  EXPECT_EQ(tabs->GetWebContentsAt(1), first);
  ASSERT_EQ(tabs->active_index(), 1);
  ExpectURL(tabs->GetActiveWebContents(), first_url);

  tabs->ActivateTabAt(0);
  ASSERT_EQ(tabs->active_index(), 0);
  tabs->CloseWebContentsAt(1, 0);
  ASSERT_EQ(tabs->count(), 1);
  EXPECT_EQ(tabs->GetActiveWebContents(), second);
  ExpectURL(tabs->GetActiveWebContents(), second_url);
}
