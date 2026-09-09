// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_BROWSER_BROWSER_CONFIG_GATE_H_
#define MB_BROWSER_BROWSER_CONFIG_GATE_H_

#include <optional>

#include "base/component_export.h"
#include "mb/config/runtime_config.h"

namespace mb {

// Linux BasicStartupComplete hook, after Chromium's explicit-root web-security
// guard and remote-debugging-pipe descriptor validation. Children skip it.
// Only explicit --config enables configuration loading. Returns an exit code
// on failure; does not create a default config or remember a selected name.
COMPONENT_EXPORT(MB_BROWSER_CONFIG)
std::optional<int> InitializeExplicitBrowserConfig();

// Null for children and browser processes without explicit --config. Published
// once on successful startup, immutable thereafter, and owned for process life.
COMPONENT_EXPORT(MB_BROWSER_CONFIG)
const config::RuntimeConfig* GetProcessRuntimeConfig();

}  // namespace mb

#endif  // MB_BROWSER_BROWSER_CONFIG_GATE_H_
