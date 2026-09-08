// SPDX-License-Identifier: BSD-3-Clause

#include "mb/browser/browser_window_adapter.h"

#include <utility>

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

namespace mb {
namespace {

std::optional<int> CommandId(BrowserCommand command) {
  switch (command) {
    case BrowserCommand::kNewTab:
      return IDC_NEW_TAB;
    case BrowserCommand::kCloseTab:
      return IDC_CLOSE_TAB;
    case BrowserCommand::kReopenTab:
      return IDC_RESTORE_TAB;
    case BrowserCommand::kNextTab:
      return IDC_SELECT_NEXT_TAB;
    case BrowserCommand::kPreviousTab:
      return IDC_SELECT_PREVIOUS_TAB;
    case BrowserCommand::kFocusLocation:
      return IDC_FOCUS_LOCATION;
    case BrowserCommand::kNewIncognitoWindow:
      return IDC_NEW_INCOGNITO_WINDOW;
    case BrowserCommand::kDevTools:
      return IDC_DEV_TOOLS;
  }
  return std::nullopt;
}

TabStripModel* TabModel(Browser* browser) {
  return browser ? browser->GetTabStripModel() : nullptr;
}

}  // namespace

TabCollectionAdapter::TabCollectionAdapter(Browser* browser)
    : browser_(browser) {}

int TabCollectionAdapter::Count() const {
  TabStripModel* model = TabModel(browser_);
  return model ? model->count() : 0;
}

std::vector<TabSnapshot> TabCollectionAdapter::Snapshot() const {
  std::vector<TabSnapshot> tabs;
  const int count = Count();
  tabs.reserve(count);
  for (int index = 0; index < count; ++index) {
    if (std::optional<TabSnapshot> tab = SnapshotAt(index)) {
      tabs.push_back(std::move(*tab));
    }
  }
  return tabs;
}

std::optional<TabSnapshot> TabCollectionAdapter::At(int index) const {
  return SnapshotAt(index);
}

std::optional<TabSnapshot> TabCollectionAdapter::Active() const {
  TabStripModel* model = TabModel(browser_);
  return model ? SnapshotAt(model->active_index()) : std::nullopt;
}

bool TabCollectionAdapter::Select(tabs::TabHandle handle) const {
  std::optional<int> index = Resolve(handle);
  if (!index) {
    return false;
  }
  TabModel(browser_)->ActivateTabAt(*index);
  return true;
}

bool TabCollectionAdapter::Close(tabs::TabHandle handle) const {
  std::optional<int> index = Resolve(handle);
  if (!index) {
    return false;
  }
  TabModel(browser_)->CloseWebContentsAt(
      *index, CLOSE_USER_GESTURE | CLOSE_CREATE_HISTORICAL_TAB);
  return true;
}

std::optional<int> TabCollectionAdapter::Move(tabs::TabHandle handle,
                                              int target_index) const {
  std::optional<int> index = Resolve(handle);
  TabStripModel* model = TabModel(browser_);
  if (!index || !model || !model->ContainsIndex(target_index)) {
    return std::nullopt;
  }
  return model->MoveWebContentsAt(*index, target_index,
                                  /*select_after_move=*/false);
}

bool TabCollectionAdapter::SetPinned(tabs::TabHandle handle,
                                     bool pinned) const {
  std::optional<int> index = Resolve(handle);
  if (!index) {
    return false;
  }
  TabModel(browser_)->SetTabPinned(*index, pinned);
  return true;
}

std::optional<int> TabCollectionAdapter::Resolve(tabs::TabHandle handle) const {
  tabs::TabInterface* tab = handle.Get();
  TabStripModel* model = TabModel(browser_);
  if (!tab || !model) {
    return std::nullopt;
  }

  const int index = model->GetIndexOfTab(tab);
  if (!model->ContainsIndex(index) || model->GetTabAtIndex(index) != tab) {
    return std::nullopt;
  }
  return index;
}

std::optional<TabSnapshot> TabCollectionAdapter::SnapshotAt(int index) const {
  TabStripModel* model = TabModel(browser_);
  if (!model || !model->ContainsIndex(index)) {
    return std::nullopt;
  }

  tabs::TabInterface* tab = model->GetTabAtIndex(index);
  if (!tab) {
    return std::nullopt;
  }
  return TabSnapshot{.handle = tab->GetHandle(),
                     .index = index,
                     .active = index == model->active_index(),
                     .pinned = model->IsTabPinned(index),
                     .title = tab->GetTitle(),
                     .url = tab->GetURL()};
}

CommandAdapter::CommandAdapter(Browser* browser) : browser_(browser) {}

bool CommandAdapter::CanExecute(BrowserCommand command) const {
  std::optional<int> command_id = CommandId(command);
  return browser_ && command_id &&
         chrome::IsCommandEnabled(browser_, *command_id);
}

bool CommandAdapter::Execute(BrowserCommand command) const {
  std::optional<int> command_id = CommandId(command);
  return browser_ && command_id &&
         chrome::ExecuteCommand(browser_, *command_id);
}

BrowserWindowAdapter::BrowserWindowAdapter(Browser* browser)
    : browser_(browser) {}

bool BrowserWindowAdapter::is_incognito() const {
  return browser_ && browser_->GetProfile() &&
         browser_->GetProfile()->IsOffTheRecord();
}

TabCollectionAdapter BrowserWindowAdapter::tabs() const {
  return TabCollectionAdapter(browser_);
}

CommandAdapter BrowserWindowAdapter::commands() const {
  return CommandAdapter(browser_);
}

}  // namespace mb
