// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_BROWSER_RUNTIME_UI_CONFIG_H_
#define MB_BROWSER_RUNTIME_UI_CONFIG_H_
#include <optional>

#include "base/component_export.h"
#include "base/memory/raw_ptr.h"
#include "mb/config/config.h"
namespace base {
class SupportsUserData;
}
class PrefService;
class PrefRegistrySimple;
namespace mb {
COMPONENT_EXPORT(MB_RUNTIME_UI_CONFIG)
void RegisterRuntimeUiConfigPrefs(PrefRegistrySimple* registry);
// Once per regular profile per process. Returns a changed theme to apply
// through ThemeService; record it only after that native operation has
// completed.
COMPONENT_EXPORT(MB_RUNTIME_UI_CONFIG)
std::optional<config::Theme> InitializeRuntimeUiConfig(
    base::SupportsUserData* profile,
    PrefService* prefs,
    const config::UiConfig& config);
COMPONENT_EXPORT(MB_RUNTIME_UI_CONFIG)
void RecordRuntimeThemeApplied(base::SupportsUserData* profile);
// Called before/after the native controller is constructed, normal windows
// only.
COMPONENT_EXPORT(MB_RUNTIME_UI_CONFIG)
void OverrideInitialSidebarConfig(base::SupportsUserData* profile,
                                  bool restored,
                                  std::optional<bool>& collapsed,
                                  std::optional<int>& width);
COMPONENT_EXPORT(MB_RUNTIME_UI_CONFIG)
void DidInitializeSidebarConfig(base::SupportsUserData* profile);
// Scope only the native initial synchronous restoration, never later user
// restores or singleton activation. Profile outlives the synchronous operation.
class COMPONENT_EXPORT(MB_RUNTIME_UI_CONFIG) ScopedInitialUiConfigRestore {
 public:
  ScopedInitialUiConfigRestore(base::SupportsUserData* profile,
                               bool initial_process_startup);
  ~ScopedInitialUiConfigRestore();
  ScopedInitialUiConfigRestore(const ScopedInitialUiConfigRestore&) = delete;
  ScopedInitialUiConfigRestore& operator=(const ScopedInitialUiConfigRestore&) =
      delete;

 private:
  raw_ptr<base::SupportsUserData> profile_ = nullptr;
};
}  // namespace mb
#endif
