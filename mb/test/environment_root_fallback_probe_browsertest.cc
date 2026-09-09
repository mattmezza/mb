// SPDX-License-Identifier: BSD-3-Clause
// Disabled, externally verified startup-exit probe. Never part of a wildcard run.
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/test/scoped_path_override.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_paths_internal.h"
#include "chrome/test/base/chrome_test_launcher.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "mb/browser/browser_config_gate.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
constexpr char kFilter[] =
    "MbEnvironmentRootFallbackProbe.DISABLED_ExitBeforeNativeDefaultFallback";
constexpr char kMarker[] = "mb-environment-root-fallback-probe-v1\n";
constexpr char kReplacement[] = "owned fallback probe obstacle\n";

class FallbackProbeDelegate;
FallbackProbeDelegate* g_probe_delegate = nullptr;

class FallbackProbeDelegate : public ChromeTestChromeMainDelegate {
 public:
  explicit FallbackProbeDelegate(base::FilePath root) : root_(std::move(root)) {}
  void PreparePathOverride() {
    path_override_ = std::make_unique<base::ScopedPathOverride>(
        chrome::DIR_USER_DATA, root_.AppendASCII("selected"), true, false);
  }

 protected:
  std::optional<int> BasicStartupComplete() override {
    // Includes the real 0008 gate, after native security/pipe checks.
    if (auto result = ChromeTestChromeMainDelegate::BasicStartupComplete()) {
      return result;
    }
    const auto* snapshot = mb::GetProcessRuntimeConfig();
    const base::FilePath selected = root_.AppendASCII("selected");
    if (!snapshot || snapshot->selected_root != selected.value() ||
        snapshot->config_filename != root_.AppendASCII("config.toml").value()) {
      return ProbeFailure();
    }
    base::FilePath default_root;
    if (!chrome::GetDefaultUserDataDirectory(&default_root) ||
        !root_.AppendASCII("xdg").IsParent(default_root) ||
        base::PathExists(default_root)) {
      return ProbeFailure();
    }
    // InProcessBrowserTest installs this override before the gate. Production
    // has none yet; remove the fixture override so failed native initialization
    // can actually reach its normal default-path provider if 0009 is missing.
    path_override_.reset();
    if (base::PathService::IsOverriddenForTesting(chrome::DIR_USER_DATA)) {
      return ProbeFailure();
    }
    // All mutation is beneath the runner-created private root. Rename retains
    // the gate's open directory inode and is independent of chmod/root-user rules.
    if (base::PathExists(root_.AppendASCII("prepared-backup")) ||
        !base::Move(selected, root_.AppendASCII("prepared-backup")) ||
        !base::WriteFile(selected, kReplacement) ||
        !base::WriteFile(root_.AppendASCII("native-default-path"),
                         default_root.value()) ||
        !base::WriteFile(root_.AppendASCII("after-gate-checkpoint"), kMarker)) {
      return ProbeFailure();
    }
    return std::nullopt;
  }

  void PreSandboxStartup() override {
    ChromeTestChromeMainDelegate::PreSandboxStartup();
    // If 0009 did not exit, stop before singleton/Profile/browser-window startup.
    // Distinct status makes the external verifier reject a missing interception.
    std::cerr << "fallback probe: native PreSandboxStartup unexpectedly returned\n";
    base::Process::TerminateCurrentProcessImmediately(97);
  }

 private:
  static std::optional<int> ProbeFailure() {
    std::cerr << "fallback probe: fixture precondition failed\n";
    return 99;
  }
  const base::FilePath root_;
  std::unique_ptr<base::ScopedPathOverride> path_override_;
};

class MbEnvironmentRootFallbackProbe : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    const auto value = base::Environment::Create()->GetVar("MB_TEST_FALLBACK_PROBE_ROOT");
    // DISABLED_ excludes ordinary runs; these guards also exclude broad
    // --gtest_also_run_disabled_tests runs. The env key exists only in this test.
    if (!value || !command_line.HasSwitch("single-process-tests") ||
        !command_line.HasSwitch("gtest_also_run_disabled_tests") ||
        command_line.GetSwitchValueNative("gtest_filter") != kFilter) {
      GTEST_SKIP() << "Requires the dedicated external fallback probe runner";
    }
    const base::FilePath root(*value);
    struct stat st;
    std::string marker;
    if (!root.IsAbsolute() || lstat(root.value().c_str(), &st) != 0 ||
        !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0777) != 0700 ||
        !base::ReadFileToString(root.AppendASCII("probe-marker"), &marker) ||
        marker != kMarker ||
        command_line.GetSwitchValuePath("config") != root.AppendASCII("config.toml") ||
        command_line.GetSwitchValuePath("user-data-dir") != root.AppendASCII("selected")) {
      GTEST_SKIP() << "Requires an isolated runner-owned fallback fixture";
    }
    ASSERT_FALSE(base::PathService::IsOverriddenForTesting(chrome::DIR_USER_DATA));
    // Install the RAII owner before the native fixture refreshes this override.
    // Its destruction in the delegate restores the original absence of an
    // override, using the public test API rather than private PathService calls.
    ASSERT_NE(g_probe_delegate, nullptr);
    g_probe_delegate->PreparePathOverride();
    started_ = true;
    InProcessBrowserTest::SetUp();
  }

  void TearDown() override {
    if (started_) {
      InProcessBrowserTest::TearDown();
    }
  }

 private:
  bool started_ = false;
};

IN_PROC_BROWSER_TEST_F(MbEnvironmentRootFallbackProbe,
                       DISABLED_ExitBeforeNativeDefaultFallback) {
  FAIL() << "The startup exit probe must never enter a browser test body";
}
}  // namespace

// Called once by the product test launcher, before any ChromeMainDelegate exists.
content::ContentMainDelegate* CreateMbBrowserTestDelegate() {
  const auto& command = *base::CommandLine::ForCurrentProcess();
  const auto root = base::Environment::Create()->GetVar("MB_TEST_FALLBACK_PROBE_ROOT");
  if (root && command.HasSwitch("single-process-tests") &&
      command.HasSwitch("gtest_also_run_disabled_tests") &&
      command.GetSwitchValueNative("gtest_filter") == kFilter) {
    g_probe_delegate = new FallbackProbeDelegate(base::FilePath(*root));
    return g_probe_delegate;
  }
  return new ChromeTestChromeMainDelegate();
}
