// SPDX-License-Identifier: BSD-3-Clause
#include "mb/browser/runtime_ui_config.h"

#include <memory>
#include <string_view>

#include "base/check.h"
#include "base/supports_user_data.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
namespace mb {
namespace {
constexpr char kLastApplied[] = "mb.runtime_ui.last_applied";
constexpr char kStateKey[] = "mb.runtime_ui.process_state";
struct State : base::SupportsUserData::Data {
  State(PrefService* pref_service, const config::UiConfig& value)
      : prefs(pref_service), desired(value) {}
  raw_ptr<PrefService> prefs;
  config::UiConfig desired;
  bool width_changed = false;
  bool collapse_changed = false;
  bool theme_changed = false;
  bool sidebar_initialized = false;
  bool initialization_pending = false;
  int restore_depth = 0;
};
State* GetState(base::SupportsUserData* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<State*>(profile->GetUserData(kStateKey));
}
std::string_view ThemeName(config::Theme theme) {
  switch (theme) {
    case config::Theme::kSystem:
      return "system";
    case config::Theme::kLight:
      return "light";
    case config::Theme::kDark:
      return "dark";
  }
  return "system";
}
void RecordSidebarApplied(State& state) {
  if (!state.sidebar_initialized || state.restore_depth != 0) {
    return;
  }
  ScopedDictPrefUpdate update(state.prefs, kLastApplied);
  if (state.width_changed) {
    update->Set("sidebar_width", state.desired.sidebar_width);
  }
  if (state.collapse_changed) {
    update->Set("sidebar_collapsed", state.desired.sidebar_collapsed);
  }
  state.width_changed = false;
  state.collapse_changed = false;
}
}  // namespace
void RegisterRuntimeUiConfigPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kLastApplied);
}
std::optional<config::Theme> InitializeRuntimeUiConfig(
    base::SupportsUserData* profile,
    PrefService* prefs,
    const config::UiConfig& config) {
  if (!profile || GetState(profile)) {
    return std::nullopt;
  }
  CHECK(prefs);
  auto state = std::make_unique<State>(prefs, config);
  const auto& previous = prefs->GetDict(kLastApplied);
  state->width_changed =
      previous.FindInt("sidebar_width") != config.sidebar_width;
  state->collapse_changed =
      previous.FindBool("sidebar_collapsed") != config.sidebar_collapsed;
  const auto* old_theme = previous.FindString("theme");
  state->theme_changed = !old_theme || *old_theme != ThemeName(config.theme);
  if (state->width_changed) {
    prefs->SetInteger(prefs::kVerticalTabsUncollapsedWidth,
                      config.sidebar_width);
  }
  if (state->collapse_changed) {
    prefs->SetBoolean(prefs::kVerticalTabsCollapsedState,
                      config.sidebar_collapsed);
  }
  const bool changed_theme = state->theme_changed;
  profile->SetUserData(kStateKey, std::move(state));
  return changed_theme ? std::optional(config.theme) : std::nullopt;
}
void RecordRuntimeThemeApplied(base::SupportsUserData* profile) {
  State* state = GetState(profile);
  if (!state || !state->theme_changed) {
    return;
  }
  ScopedDictPrefUpdate update(state->prefs, kLastApplied);
  update->Set("theme", ThemeName(state->desired.theme));
  state->theme_changed = false;
}
void OverrideInitialSidebarConfig(base::SupportsUserData* profile,
                                  bool restored,
                                  std::optional<bool>& collapsed,
                                  std::optional<int>& width) {
  State* state = GetState(profile);
  if (!state || (state->sidebar_initialized && state->restore_depth == 0) ||
      (restored && state->restore_depth == 0)) {
    return;
  }
  if (state->collapse_changed) {
    collapsed = state->desired.sidebar_collapsed;
  }
  if (state->width_changed) {
    width = state->desired.sidebar_width;
  }
  state->initialization_pending = true;
}
void DidInitializeSidebarConfig(base::SupportsUserData* profile) {
  State* state = GetState(profile);
  if (!state || !state->initialization_pending) {
    return;
  }
  state->initialization_pending = false;
  state->sidebar_initialized = true;
  RecordSidebarApplied(*state);
}
ScopedInitialUiConfigRestore::ScopedInitialUiConfigRestore(
    base::SupportsUserData* profile,
    bool initial_process_startup) {
  if (initial_process_startup) {
    if (State* state = GetState(profile)) {
      profile_ = profile;
      ++state->restore_depth;
    }
  }
}
ScopedInitialUiConfigRestore::~ScopedInitialUiConfigRestore() {
  if (!profile_) {
    return;
  }
  State* state = GetState(profile_);
  CHECK(state);
  CHECK_GT(state->restore_depth, 0);
  --state->restore_depth;
  RecordSidebarApplied(*state);
}
}  // namespace mb
