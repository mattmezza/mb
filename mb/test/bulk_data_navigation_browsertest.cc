// SPDX-License-Identifier: BSD-3-Clause
// Separate diagnostic: real bulk data navigation, with native child exits.

#include <algorithm>
#include <memory>
#include <string>

#include "base/base64.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/gtest_tags.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/time/time.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_delegate.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/child_process_termination_info.h"
#include "content/public/browser/network_service_util.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

void Tag(const std::string& key, int64_t value) {
  base::AddTagToTestResult(key, base::NumberToString(value));
}

std::string PageTitle(int index) {
  return "BulkData" + base::NumberToString(index);
}

GURL DataPage(int index) {
  const std::string html = "<!doctype html><meta charset=utf-8><title>" +
                           PageTitle(index) + "</title><p>Local fixture</p>";
  return GURL("data:text/html;base64," + base::Base64Encode(html));
}

class NetworkExitRecorder : public content::BrowserChildProcessObserver {
 public:
  NetworkExitRecorder() : start_(base::TimeTicks::Now()) { Add(this); }
  ~NetworkExitRecorder() override {
    Remove(this);
    Tag("bulk_data_network_event_count", event_count_);
  }

  void BrowserChildProcessLaunchedAndConnected(
      const content::ChildProcessData& data) override {
    Record("launch", data, nullptr);
  }
  void BrowserChildProcessHostDisconnected(
      const content::ChildProcessData& data) override {
    // Disconnect ordering differs from termination callbacks. Record it without
    // inferring a cause. Disconnection and termination are both failures.
    Record("disconnect", data, nullptr);
    if (data.metrics_name == network::mojom::NetworkService::Name_) {
      ADD_FAILURE() << "Network service disconnected during bulk diagnostic";
    }
  }
  void BrowserChildProcessCrashed(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override {
    Record("crashed", data, &info);
  }
  void BrowserChildProcessKilled(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override {
    Record("killed", data, &info);
  }
  void BrowserChildProcessLaunchFailed(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override {
    Record("launch_failed", data, &info);
  }
  void BrowserChildProcessExitedNormally(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override {
    Record("normal_exit", data, &info);
  }

 private:
  void Record(const char* event,
              const content::ChildProcessData& data,
              const content::ChildProcessTerminationInfo* info) {
    if (data.metrics_name != network::mojom::NetworkService::Name_) {
      return;
    }
    const std::string prefix =
        "bulk_data_network_" + base::NumberToString(event_count_++) + "_";
    // Only fixed event labels and native numeric metadata, never URLs/argv.
    base::AddTagToTestResult(prefix + "event", event);
    Tag(prefix + "observed_us",
        (base::TimeTicks::Now() - start_).InMicroseconds());
    Tag(prefix + "observed_unix_ms",
        base::Time::Now().InMillisecondsSinceUnixEpoch());
    Tag(prefix + "pid",
        data.GetProcess().IsValid() ? data.GetProcess().Pid() : -1);
    if (info) {
      Tag(prefix + "status", static_cast<int>(info->status));
      Tag(prefix + "exit_code", info->exit_code);
      ADD_FAILURE() << "Network service " << event
                    << "; native status=" << static_cast<int>(info->status)
                    << "; native exit_code=" << info->exit_code;
    }
  }

  const base::TimeTicks start_;
  int event_count_ = 0;
};

class MbBulkDataNavigationBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    recorder_ = std::make_unique<NetworkExitRecorder>();
    // Begin from a real ready browser; this is not raw-argv bulk startup.
    base::test::ScopedRunLoopTimeout ready_timeout(FROM_HERE,
                                                   base::Seconds(30));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
    ASSERT_TRUE(content::IsOutOfProcessNetworkService());
    int network_count = 0;
    for (const auto& info :
         content::ServiceProcessHost::GetRunningProcessInfo()) {
      if (info.IsService<network::mojom::NetworkService>()) {
        ASSERT_TRUE(info.GetProcess().IsValid());
        Tag("bulk_data_initial_network_pid", info.GetProcess().Pid());
        ++network_count;
      }
    }
    Tag("bulk_data_initial_network_count", network_count);
    ASSERT_GT(network_count, 0);
  }

  void TearDownOnMainThread() override {
    // Exclude intentional fixture/browser shutdown from the diagnostic window.
    recorder_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 private:
  std::unique_ptr<NetworkExitRecorder> recorder_;
};

IN_PROC_BROWSER_TEST_F(
    MbBulkDataNavigationBrowserTest,
    OneHundredLocalDataPagesFinishWithoutNetworkServiceExit) {
  constexpr int kPageCount = 100;
  TabStripModel* const tabs = browser()->tab_strip_model();
  ASSERT_EQ(tabs->count(), 1);
  const base::TimeTicks start = base::TimeTicks::Now();
  const base::TimeTicks deadline = start + base::Minutes(3);

  // Every data page goes through native AddTabAt. Remove only the initial blank
  // control tab after opening the first data tab, leaving exactly 100 data
  // tabs.
  for (int index = 0; index < kPageCount; ++index) {
    SCOPED_TRACE(index);
    ASSERT_LT(base::TimeTicks::Now(), deadline);
    tabs->delegate()->AddTabAt(DataPage(index), -1, /*foreground=*/index == 0);
    if (index == 0) {
      tabs->CloseWebContentsAt(0, /*close_types=*/0);
    }
  }
  Tag("bulk_data_mutation_us",
      (base::TimeTicks::Now() - start).InMicroseconds());
  ASSERT_EQ(tabs->count(), kPageCount);

  for (int index = 0; index < kPageCount; ++index) {
    SCOPED_TRACE(index);
    const base::TimeDelta remaining = deadline - base::TimeTicks::Now();
    ASSERT_GT(remaining, base::TimeDelta());
    base::test::ScopedRunLoopTimeout load_timeout(
        FROM_HERE, std::min(remaining, base::Seconds(30)));
    content::WebContents* const contents = tabs->GetWebContentsAt(index);
    Tag("bulk_data_waiting_page", index);
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    ASSERT_FALSE(contents->IsCrashed());
    // Avoid assertions that would print the complete data URL on failure.
    ASSERT_TRUE(contents->GetLastCommittedURL() == DataPage(index));
    ASSERT_EQ(contents->GetTitle(), base::UTF8ToUTF16(PageTitle(index)));
    Tag("bulk_data_completed_page", index);
  }
  Tag("bulk_data_all_loaded_us",
      (base::TimeTicks::Now() - start).InMicroseconds());
}

}  // namespace
