// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_APP_CHROME_STARTUP_ARGUMENTS_H_
#define MB_APP_CHROME_STARTUP_ARGUMENTS_H_

#include <string>
#include <vector>

#include "mb/app/startup_arguments.h"

namespace mb {

// Linux ChromeMain adapter; does not initialize the global CommandLine, open
// files, or read/change the environment. extra_argv is the once-tokenized
// Chromium extra-flags stream, including its empty program sentinel at [0].
// Nonempty Chromium --type invocations return the original argv unchanged.
// Browser selectors and process/root identity are accepted only in raw_argv.
StartupArgumentsResult NormalizeChromeStartupArguments(
    const std::vector<std::string>& raw_argv,
    const std::vector<std::string>& extra_argv);

}  // namespace mb

#endif  // MB_APP_CHROME_STARTUP_ARGUMENTS_H_
