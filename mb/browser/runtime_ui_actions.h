// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_BROWSER_RUNTIME_UI_ACTIONS_H_
#define MB_BROWSER_RUNTIME_UI_ACTIONS_H_
#include <vector>

#include "mb/browser/browser_config_gate.h"
#include "url/gurl.h"
namespace mb {
// Restart-only policy; true preserves all native pin/hover/focus/width rules.
inline bool ShouldShowConfiguredTabCloseButtons(bool normal_window) {
  const auto* runtime = GetProcessRuntimeConfig();
  return !normal_window || !runtime ||
         runtime->config.ui.show_tab_close_buttons;
}
// Native startup eligibility/precedence is enforced by the caller. No argv or
// PrefService mutation. The gate validated every URL before preparing the root.
inline std::vector<GURL> GetConfiguredStartupUrls() {
  const auto* runtime = GetProcessRuntimeConfig();
  if (!runtime) {
    return {};
  }
  for (const auto& environment : runtime->config.environments) {
    if (environment.name != runtime->selected_environment) {
      continue;
    }
    std::vector<GURL> urls;
    urls.reserve(environment.startup_urls.size());
    for (const auto& raw : environment.startup_urls) {
      urls.emplace_back(raw);
    }
    return urls;
  }
  return {};
}
}  // namespace mb
#endif
