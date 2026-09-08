// SPDX-License-Identifier: BSD-3-Clause

#ifndef MB_BROWSER_BROWSER_WINDOW_ADAPTER_H_
#define MB_BROWSER_BROWSER_WINDOW_ADAPTER_H_

#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/tabs/public/tab_interface.h"
#include "url/gurl.h"

class Browser;

namespace mb {

// A point-in-time representation of a tab. Call TabCollectionAdapter again to
// obtain current state; this type is deliberately not kept in sync by an
// observer.
struct TabSnapshot {
  tabs::TabHandle handle;
  int index = 0;
  bool active = false;
  bool pinned = false;
  std::u16string title;
  GURL url;
};

// Accesses a Browser's live TabStripModel. All use is on the browser UI thread,
// and the adapter must not outlive its Browser.
class TabCollectionAdapter {
 public:
  explicit TabCollectionAdapter(Browser* browser);
  TabCollectionAdapter(const TabCollectionAdapter&) = default;
  TabCollectionAdapter& operator=(const TabCollectionAdapter&) = delete;
  ~TabCollectionAdapter() = default;

  int Count() const;
  std::vector<TabSnapshot> Snapshot() const;
  std::optional<TabSnapshot> At(int index) const;
  std::optional<TabSnapshot> Active() const;

  // Each mutation resolves |handle| against the current model. A destroyed,
  // detached, or cross-window handle is rejected without touching the model.
  bool Select(tabs::TabHandle handle) const;
  bool Close(tabs::TabHandle handle) const;
  // Returns the actual index after TabStripModel applies pin and group
  // constraints, or nullopt for a stale handle or invalid requested index.
  std::optional<int> Move(tabs::TabHandle handle, int target_index) const;
  bool SetPinned(tabs::TabHandle handle, bool pinned) const;

 private:
  std::optional<int> Resolve(tabs::TabHandle handle) const;
  std::optional<TabSnapshot> SnapshotAt(int index) const;

  const raw_ptr<Browser> browser_;
};

enum class BrowserCommand {
  kNewTab,
  kCloseTab,
  kReopenTab,
  kNextTab,
  kPreviousTab,
  kFocusLocation,
  kNewIncognitoWindow,
  kDevTools,
};

// Executes existing Chromium browser commands; it owns no command state.
class CommandAdapter {
 public:
  explicit CommandAdapter(Browser* browser);
  CommandAdapter(const CommandAdapter&) = default;
  CommandAdapter& operator=(const CommandAdapter&) = delete;
  ~CommandAdapter() = default;

  bool CanExecute(BrowserCommand command) const;
  bool Execute(BrowserCommand command) const;

 private:
  const raw_ptr<Browser> browser_;
};

// Small composition point for product window chrome. It provides live tab and
// command adapters and queries the Browser's Profile directly.
class BrowserWindowAdapter {
 public:
  explicit BrowserWindowAdapter(Browser* browser);
  BrowserWindowAdapter(const BrowserWindowAdapter&) = default;
  BrowserWindowAdapter& operator=(const BrowserWindowAdapter&) = delete;
  ~BrowserWindowAdapter() = default;

  bool is_incognito() const;
  TabCollectionAdapter tabs() const;
  CommandAdapter commands() const;

 private:
  const raw_ptr<Browser> browser_;
};

}  // namespace mb

#endif  // MB_BROWSER_BROWSER_WINDOW_ADAPTER_H_
