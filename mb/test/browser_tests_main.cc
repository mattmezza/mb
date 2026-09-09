// SPDX-License-Identifier: BSD-3-Clause
// Native Chrome test launcher with one product test-delegate factory.
#include "base/command_line.h"
#include "base/test/launcher/test_launcher.h"
#include "base/test/test_switches.h"
#include "chrome/test/base/chrome_test_launcher.h"
#include "ui/compositor/compositor_switches.h"

content::ContentMainDelegate* CreateMbBrowserTestDelegate();
namespace {
class MbTestLauncherDelegate : public ChromeTestLauncherDelegate {
 public:
  using ChromeTestLauncherDelegate::ChromeTestLauncherDelegate;
 protected:
  content::ContentMainDelegate* CreateContentMainDelegate() override {
    return CreateMbBrowserTestDelegate();
  }
};
}
int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  const size_t jobs = base::NumParallelJobs(2);
  if (!jobs) return 1;
  auto* command = base::CommandLine::ForCurrentProcess();
  if (command->HasSwitch(switches::kTestLauncherInteractive))
    command->AppendSwitch(switches::kEnablePixelOutputInTests);
  ChromeTestSuiteRunner runner;
  MbTestLauncherDelegate delegate(&runner);
  return LaunchChromeTests(jobs, &delegate, argc, argv);
}
