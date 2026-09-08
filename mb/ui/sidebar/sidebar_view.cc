#include "mb/ui/sidebar/sidebar_view.h"

#include <string_view>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/tabs/vertical/vertical_tab_strip_top_container.h"
#include "mb/generated/branding/branding.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/view_class_properties.h"

namespace mb {
namespace {

std::optional<BrowserCommand> MenuCommand(int command_id) {
  switch (command_id) {
    case 1:
      return BrowserCommand::kNewTab;
    case 2:
      return BrowserCommand::kReopenTab;
    case 3:
      return BrowserCommand::kNewIncognitoWindow;
    case 4:
      return BrowserCommand::kDevTools;
    default:
      return std::nullopt;
  }
}

}  // namespace

SidebarView::SidebarView(
    tabs::VerticalTabStripStateController* state_controller,
    actions::ActionItem* root_action_item,
    BrowserView* browser_view)
    : VerticalTabStripRegionView(state_controller,
                                 root_action_item,
                                 browser_view),
      window_(browser_view->browser()) {
  const auto top_index = GetIndexOf(GetTopContainer());
  CHECK(top_index.has_value());
  header_ = AddChildViewAt(std::make_unique<views::View>(), *top_index + 1);
  header_->SetProperty(views::kMarginsKey, gfx::Insets::VH(8, 12));
  header_->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

  auto* text = header_->AddChildView(std::make_unique<views::View>());
  text->SetLayoutManager(std::make_unique<views::FlexLayout>())
      ->SetOrientation(views::LayoutOrientation::kVertical)
      .SetCrossAxisAlignment(views::LayoutAlignment::kStart);
  text->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                               views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  std::u16string name =
      base::UTF8ToUTF16(std::string_view(branding::kFullName));
  if (window_.is_incognito()) {
    name += u" — Private";
  }
  auto* title = text->AddChildView(std::make_unique<views::Label>(name));
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetTooltipText(name);
  count_ = text->AddChildView(std::make_unique<views::Label>());
  count_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  menu_button_ = header_->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&SidebarView::ShowCommands, base::Unretained(this)),
      u"⋯"));
  menu_button_->GetViewAccessibility().SetName(u"Browser commands");
  menu_button_->SetTooltipText(u"Browser commands");
  tab_strip_model()->AddObserver(this);
  UpdateHeader();
}

SidebarView::~SidebarView() {
  menu_runner_.reset();
  TabStripModelObserver::StopObservingAll(this);
}

void SidebarView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  VerticalTabStripRegionView::OnBoundsChanged(previous_bounds);
  if (header_) {
    header_->SetVisible(width() >= kUncollapsedMinWidth);
  }
}

void SidebarView::OnTabStripModelChanged(
    TabStripModel* model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  UpdateHeader();
}

void SidebarView::UpdateHeader() {
  const int count = window_.tabs().Count();
  count_->SetText(base::NumberToString16(count) +
                  (count == 1 ? u" tab" : u" tabs"));
}

void SidebarView::ShowCommands(const ui::Event& event) {
  menu_runner_.reset();
  menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  menu_model_->AddItem(1, u"New tab");
  menu_model_->AddItem(2, u"Reopen closed tab");
  menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  menu_model_->AddItem(3, u"New private window");
  menu_model_->AddItem(4, u"Developer tools");
  menu_hover_lock_ =
      GetExpandOnHoverLock(ExpandOnHoverLockType::kKeepCurrentState);
  menu_runner_ = std::make_unique<views::MenuRunner>(
      menu_model_.get(), views::MenuRunner::HAS_MNEMONICS,
      base::BindRepeating(&SidebarView::OnMenuClosed, base::Unretained(this)));
  menu_runner_->RunMenuAt(
      GetWidget(), nullptr, menu_button_->GetBoundsInScreen(),
      views::MenuAnchorPosition::kTopRight,
      event.IsKeyEvent() ? ui::mojom::MenuSourceType::kKeyboard
                         : ui::mojom::MenuSourceType::kMouse);
}

void SidebarView::OnMenuClosed() {
  menu_hover_lock_.reset();
}

bool SidebarView::IsCommandIdChecked(int command_id) const {
  return false;
}

bool SidebarView::IsCommandIdEnabled(int command_id) const {
  auto command = MenuCommand(command_id);
  return command && window_.commands().CanExecute(*command);
}

void SidebarView::ExecuteCommand(int command_id, int event_flags) {
  if (auto command = MenuCommand(command_id)) {
    window_.commands().Execute(*command);
  }
}

BEGIN_METADATA(SidebarView)
END_METADATA

}  // namespace mb
