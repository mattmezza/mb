// SPDX-License-Identifier: BSD-3-Clause
// Separate diagnostic: real bulk data navigation, with native child exits.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/location.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/gtest_tags.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/time/time.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/child_process_termination_info.h"
#include "content/public/browser/network_service_util.h"
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
    Tag("bulk_startup_data_network_event_count", event_count_);
  }

  void BrowserChildProcessLaunchedAndConnected(
      const content::ChildProcessData& data) override {
    Record("launch", data, nullptr);
    if (data.metrics_name == network::mojom::NetworkService::Name_) {
      ++network_launches_;
    }
  }
  int network_launches() const { return network_launches_; }
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
        "bulk_startup_data_network_" + base::NumberToString(event_count_++) + "_";
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
  int network_launches_ = 0;
};

// Test-only extra parts, owned by the native ChromeBrowserMainParts.
class StartupNetworkExtraParts : public ChromeBrowserMainExtraParts {
 public:
  // Threads now exist, so native cleanup will invoke PostMainMessageLoopRun
  // even if profile/browser startup exits before entering the test body.
  void PostCreateThreads() override {
    started_ = base::TimeTicks::Now();
    recorder_ = std::make_unique<NetworkExitRecorder>();
    Tag("bulk_startup_data_observer_started_unix_ms",
        base::Time::Now().InMillisecondsSinceUnixEpoch());
  }

  void PreProfileInit() override {
    EXPECT_TRUE(recorder_);
    RecordPhase("bulk_startup_data_pre_profile_us");
  }

  void PostBrowserStart() override {
    RecordPhase("bulk_startup_data_post_browser_start_us");
  }

  // Usual teardown explicitly stops earlier, before the fixture closes windows.
  // Registration happens only after thread creation; this also covers an early
  // exit that did not enter the test body.
  void PostMainMessageLoopRun() override { StopObserving(); }

  void StopObserving() { recorder_.reset(); }
  base::TimeTicks started() const { return started_; }
  int network_launches() const {
    return recorder_ ? recorder_->network_launches() : 0;
  }

 private:
  void RecordPhase(const char* name) {
    Tag(name, (base::TimeTicks::Now() - started_).InMicroseconds());
  }

  base::TimeTicks started_;
  std::unique_ptr<NetworkExitRecorder> recorder_;
};

class MbBulkStartupDataBrowserTest : public InProcessBrowserTest {
 public:
  MbBulkStartupDataBrowserTest() {
    set_open_about_blank_on_browser_launch(false);
  }

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Refuse unrelated positional arguments, without printing their values.
    ASSERT_TRUE(command_line->GetArgs().empty());
    for (int index = 0; index < 100; ++index) {
      command_line->AppendArg(DataPage(index).spec());
    }
  }

  void CreatedBrowserMainParts(content::BrowserMainParts* parts) override {
    // Preserve the native test infrastructure's own extra parts.
    InProcessBrowserTest::CreatedBrowserMainParts(parts);
    auto extra = std::make_unique<StartupNetworkExtraParts>();
    extra_parts_ = extra.get();
    static_cast<ChromeBrowserMainParts*>(parts)->AddParts(std::move(extra));
  }

  void TearDownOnMainThread() override {
    // ChromeBrowserMainParts still owns extra_parts_ here. Do not retain the
    // pointer after teardown; destruction of the native owner happens later.
    if (extra_parts_) {
      extra_parts_->StopObserving();
      extra_parts_ = nullptr;
    }
    InProcessBrowserTest::TearDownOnMainThread();
  }

  StartupNetworkExtraParts* extra_parts() { return extra_parts_; }

 private:
  raw_ptr<StartupNetworkExtraParts> extra_parts_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(MbBulkStartupDataBrowserTest,
                       InitialHundredDataArgumentsLoadWithoutNetworkExit) {
  ASSERT_NE(extra_parts(), nullptr);
  const base::TimeTicks start = extra_parts()->started();
  ASSERT_FALSE(start.is_null());
  Tag("bulk_startup_data_test_body_us",
      (base::TimeTicks::Now() - start).InMicroseconds());
  ASSERT_TRUE(content::IsOutOfProcessNetworkService());
  Tag("bulk_startup_data_network_launches", extra_parts()->network_launches());
  ASSERT_GT(extra_parts()->network_launches(), 0);
  TabStripModel* const tabs = browser()->tab_strip_model();
  ASSERT_EQ(tabs->count(), 100);

  // No AddTabAt, reload, fallback navigation or insertion in the test body:
  // every tab must already have come from the native initial argument path.
  const base::TimeTicks deadline = start + base::Minutes(3);
  for (int index = 0; index < 100; ++index) {
    SCOPED_TRACE(index);
    const base::TimeDelta remaining = deadline - base::TimeTicks::Now();
    ASSERT_GT(remaining, base::TimeDelta());
    base::test::ScopedRunLoopTimeout load_timeout(
        FROM_HERE, std::min(remaining, base::Seconds(30)));
    content::WebContents* const contents = tabs->GetWebContentsAt(index);
    Tag("bulk_startup_data_waiting_page", index);
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    ASSERT_FALSE(contents->IsCrashed());
    ASSERT_TRUE(contents->GetLastCommittedURL() == DataPage(index));
    ASSERT_EQ(contents->GetTitle(), base::UTF8ToUTF16(PageTitle(index)));
    Tag("bulk_startup_data_completed_page", index);
  }
  Tag("bulk_startup_data_all_loaded_us",
      (base::TimeTicks::Now() - start).InMicroseconds());
}

}  // namespace
