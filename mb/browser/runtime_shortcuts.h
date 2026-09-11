// SPDX-License-Identifier: BSD-3-Clause
#ifndef MB_BROWSER_RUNTIME_SHORTCUTS_H_
#define MB_BROWSER_RUNTIME_SHORTCUTS_H_

#include <optional>
#include <vector>

#include "ui/base/accelerators/accelerator.h"

namespace mb {

enum class RuntimeUiAction { kToggleTopBar, kToggleTabBar };

struct RuntimeShortcut {
  ui::Accelerator accelerator;
  RuntimeUiAction action;
};

// Returns the restart-only bindings from the process configuration. An empty
// configured string omits that action.
std::vector<RuntimeShortcut> GetRuntimeShortcuts();
std::optional<RuntimeUiAction> FindRuntimeUiAction(
    const ui::Accelerator& accelerator);

}  // namespace mb

#endif  // MB_BROWSER_RUNTIME_SHORTCUTS_H_
