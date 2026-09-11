// SPDX-License-Identifier: BSD-3-Clause
#include "mb/browser/runtime_shortcuts.h"

#include <string_view>

#include "mb/browser/browser_config_gate.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace mb {
namespace {

ui::Accelerator ParseShortcut(std::string_view value) {
  int modifiers = ui::EF_NONE;
  while (value.size() > 1) {
    const std::size_t separator = value.find('+');
    const std::string_view token = value.substr(0, separator);
    if (token == "Ctrl")
      modifiers |= ui::EF_CONTROL_DOWN;
    else if (token == "Alt")
      modifiers |= ui::EF_ALT_DOWN;
    else if (token == "Shift")
      modifiers |= ui::EF_SHIFT_DOWN;
    value.remove_prefix(separator + 1);
  }
  const char key = value.front();
  const ui::KeyboardCode keycode =
      key >= 'A' && key <= 'Z'
          ? static_cast<ui::KeyboardCode>(ui::VKEY_A + key - 'A')
          : static_cast<ui::KeyboardCode>(ui::VKEY_0 + key - '0');
  return ui::Accelerator(keycode, modifiers);
}

}  // namespace

std::vector<RuntimeShortcut> GetRuntimeShortcuts() {
  const auto* runtime = GetProcessRuntimeConfig();
  if (!runtime)
    return {};
  std::vector<RuntimeShortcut> shortcuts;
  const auto add = [&](const std::string& value, RuntimeUiAction action) {
    if (!value.empty())
      shortcuts.push_back({ParseShortcut(value), action});
  };
  add(runtime->config.keybindings.toggle_top_bar,
      RuntimeUiAction::kToggleTopBar);
  add(runtime->config.keybindings.toggle_tab_bar,
      RuntimeUiAction::kToggleTabBar);
  return shortcuts;
}

std::optional<RuntimeUiAction> FindRuntimeUiAction(
    const ui::Accelerator& accelerator) {
  for (const auto& shortcut : GetRuntimeShortcuts()) {
    if (shortcut.accelerator == accelerator)
      return shortcut.action;
  }
  return std::nullopt;
}

}  // namespace mb
