#ifndef MB_UI_NEW_TAB_NEW_TAB_UI_H_
#define MB_UI_NEW_TAB_NEW_TAB_UI_H_

#include <memory>
#include <string_view>

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace mb {

// Serves the same local product page at both existing regular NTP origins.
class NewTabUIConfig final : public content::WebUIConfig {
 public:
  explicit NewTabUIConfig(std::string_view host);
  ~NewTabUIConfig() override;

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// Deliberately independent of Chromium's service-backed NewTabPageUI.
class NewTabUI final : public content::WebUIController {
 public:
  NewTabUI(content::WebUI* web_ui, std::string_view host);
  ~NewTabUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace mb

#endif  // MB_UI_NEW_TAB_NEW_TAB_UI_H_
