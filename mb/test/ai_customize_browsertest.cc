// SPDX-License-Identifier: BSD-3-Clause

#include <optional>
#include <string>
#include <string_view>

#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/customize_chrome/side_panel_controller.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/omnibox/ai_mode_page_action_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_controller.h"
#include "chrome/browser/ui/omnibox/omnibox_popup_state_manager.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_placeholder_util.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/test/browser_test.h"
#include "mb/ui/new_tab/new_tab_ui.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget_utils.h"
#include "url/gurl.h"

namespace mb {
namespace {

bool HasVisibleLabelWithText(const views::View* view,
                             std::u16string_view text) {
  if (const auto* label = views::AsViewClass<const views::Label>(view);
      label && label->GetVisible() && label->GetText() == text) {
    return true;
  }
  for (const views::View* child : view->children()) {
    if (HasVisibleLabelWithText(child, text)) {
      return true;
    }
  }
  return false;
}

class MbAiCustomizeBrowserTest : public InProcessBrowserTest {
 public:
  MbAiCustomizeBrowserTest() {
    // Exercise the native RHS Ctrl+Enter UI path. This is a Chromium feature
    // configuration, not a mocked eligibility service or product policy.
    features_.InitAndEnableFeatureWithParameters(
        omnibox::kDynamicAimSubmit, {{"Omnibox_ShowRhsAimHint", "true"}});
  }

 protected:
  LocationBarView* location_bar() {
    return browser()->GetBrowserView().GetLocationBarView();
  }

  void FocusAndSetText(std::u16string_view text) {
    ASSERT_TRUE(location_bar());
    location_bar()->FocusLocation(/*is_user_initiated=*/true,
                                  /*clear_focus_if_failed=*/false);
    location_bar()->omnibox_view()->SetUserText(std::u16string(text),
                                                /*update_popup=*/true);
  }

 private:
  base::test::ScopedFeatureList features_;
};

IN_PROC_BROWSER_TEST_F(MbAiCustomizeBrowserTest,
                       ProductNtpOmitsAiAndCustomizeChromeSurfaces) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           chrome::ChromeUINewTabURLAsGURL()));
  content::WebContents* const contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(contents->GetWebUI());
  ASSERT_TRUE(contents->GetWebUI()->GetController()->GetAs<NewTabUI>());

  ASSERT_TRUE(browser()->GetActiveTabInterface());
  tabs::TabFeatures* const tab_features =
      browser()->GetActiveTabInterface()->GetTabFeatures();
  ASSERT_TRUE(tab_features);
  customize_chrome::SidePanelController* const customize_controller =
      tab_features->customize_chrome_side_panel_controller();
  ASSERT_TRUE(customize_controller);
  EXPECT_FALSE(customize_controller->IsCustomizeChromeEntryAvailable());
  EXPECT_FALSE(customize_controller->IsCustomizeChromeEntryShowing());

  FocusAndSetText(u"");
  LocationBarView* const bar = location_bar();
  ASSERT_TRUE(bar);
  // This is the actual state in which upstream displays the RHS label. Set it
  // directly to avoid suggestion-provider traffic while testing its view gate.
  bar->GetOmniboxController()->popup_state_manager()->SetPopupState(
      OmniboxPopupState::kClassic);
  static_cast<LocationBar*>(bar)->OnChanged();

  auto* const ai_controller =
      omnibox::AiModePageActionController::From(browser());
  ASSERT_TRUE(ai_controller);
  EXPECT_FALSE(omnibox::AiModePageActionController::ShouldShowPageAction(
      browser()->GetProfile(), *bar));
  EXPECT_FALSE(ai_controller->IsVisible());
  EXPECT_FALSE(omnibox::ShouldInstallAimPlaceholderText(bar));

  std::u16string placeholder;
  std::optional<std::u16string> a11y_placeholder;
  omnibox::ComputePlaceholderText(bar, placeholder, a11y_placeholder);
  EXPECT_FALSE(omnibox::IsAimPlaceholderText(bar, placeholder));
  EXPECT_FALSE(a11y_placeholder.has_value());
  EXPECT_FALSE(HasVisibleLabelWithText(bar, u"Ctrl + Enter for AI Mode"));
}

IN_PROC_BROWSER_TEST_F(MbAiCustomizeBrowserTest,
                       NativeReturnAndCtrlReturnKeepLocalNavigation) {
  ui::test::EventGenerator events(
      views::GetRootWindow(browser()->GetBrowserView().GetWidget()),
      browser()->GetBrowserView().GetNativeWindow());
  FocusAndSetText(chrome::kChromeUIVersionURL16);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return location_bar()->omnibox_view()->HasFocus(); }));
  ui_test_utils::UrlLoadObserver normal_return{
      GURL(chrome::kChromeUIVersionURL)};
  events.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  normal_return.Wait();

  FocusAndSetText(u"about:blank");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return location_bar()->omnibox_view()->HasFocus(); }));
  ui_test_utils::UrlLoadObserver ctrl_return{GURL("about:blank")};
  events.PressKey(ui::VKEY_RETURN, ui::EF_CONTROL_DOWN);
  ctrl_return.Wait();
  EXPECT_EQ(GURL("about:blank"), browser()
                                     ->tab_strip_model()
                                     ->GetActiveWebContents()
                                     ->GetLastCommittedURL());
}

}  // namespace
}  // namespace mb
