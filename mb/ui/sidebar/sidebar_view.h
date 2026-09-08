#ifndef MB_UI_SIDEBAR_SIDEBAR_VIEW_H_
#define MB_UI_SIDEBAR_SIDEBAR_VIEW_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/vertical_tab_strip_region_view.h"
#include "mb/browser/browser_window_adapter.h"
#include "ui/menus/simple_menu_model.h"

namespace views {
class Label;
class LabelButton;
class MenuRunner;
}  // namespace views

namespace mb {

// Product composition above the native single TabStripModel projection. The
// native host retains its frame, drag, focus, resize and collection contracts.
class SidebarView final : public VerticalTabStripRegionView,
                          public TabStripModelObserver,
                          public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(SidebarView, VerticalTabStripRegionView)

 public:
  SidebarView(tabs::VerticalTabStripStateController* state_controller,
              actions::ActionItem* root_action_item,
              BrowserView* browser_view);
  ~SidebarView() override;

  views::View* header_for_testing() const { return header_; }

  void OnTabStripModelChanged(
      TabStripModel* model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 protected:
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;

 private:
  void UpdateHeader();
  void ShowCommands(const ui::Event& event);
  void OnMenuClosed();

  BrowserWindowAdapter window_;
  raw_ptr<views::View> header_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
  raw_ptr<views::LabelButton> menu_button_ = nullptr;
  std::unique_ptr<ExpandOnHoverLock> menu_hover_lock_;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

}  // namespace mb

#endif  // MB_UI_SIDEBAR_SIDEBAR_VIEW_H_
