#include "mb/ui/new_tab/new_tab_ui.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/page_not_available_for_guest/page_not_available_for_guest_ui.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "mb/generated/branding/branding.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace mb {
namespace {

constexpr char kStyle[] = R"css(
:root { color-scheme: light dark; font-family: system-ui, sans-serif; }
body { margin: 0; background: light-dark(#f8f9fa, #202124);
       color: light-dark(#202124, #e8eaed); }
main { min-height: 100vh; display: flex; flex-direction: column;
       align-items: center; justify-content: center; padding: 2rem;
       box-sizing: border-box; text-align: center; }
h1 { margin: 0; font-size: 3rem; font-weight: 550; letter-spacing: -.04em; }
p { margin: 1rem 0 0; line-height: 1.5; }
)css";

std::string PageHtml() {
  const std::string name = base::EscapeForHTML(branding::kFullName);
  return base::StrCat(
      {"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       "<title>",
       name,
       "</title><link rel=\"stylesheet\" href=\"/style.css\"></head>"
       "<body><main><h1>",
       name,
       "</h1><p>Search or enter an address above.</p></main></body></html>"});
}

void HandleRequest(const std::string& html,
                   const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  const std::string_view resource =
      std::string_view(path).substr(0, path.find('?'));
  if (resource.empty() || resource == "index.html") {
    std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(html));
  } else if (resource == "style.css") {
    std::move(callback).Run(
        base::MakeRefCounted<base::RefCountedString>(kStyle));
  } else {
    // No fallback resources, strings.js, image proxy or network source.
    std::move(callback).Run(nullptr);
  }
}

}  // namespace

NewTabUIConfig::NewTabUIConfig(std::string_view host)
    : content::WebUIConfig(content::kChromeUIScheme, host) {}

NewTabUIConfig::~NewTabUIConfig() = default;

bool NewTabUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return !Profile::FromBrowserContext(browser_context)->IsOffTheRecord();
}

std::unique_ptr<content::WebUIController> NewTabUIConfig::CreateWebUIController(
    content::WebUI* web_ui,
    const GURL& url) {
  if (Profile::FromWebUI(web_ui)->IsGuestSession()) {
    return std::make_unique<PageNotAvailableForGuestUI>(web_ui, host());
  }
  return std::make_unique<NewTabUI>(web_ui, host());
}

NewTabUI::NewTabUI(content::WebUI* web_ui, std::string_view host)
    : content::WebUIController(web_ui) {
  web_ui->OverrideTitle(base::UTF8ToUTF16(std::string_view(branding::kFullName)));
  auto* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(), std::string(host));
  using Directive = network::mojom::CSPDirectiveName;
  source->OverrideContentSecurityPolicy(Directive::DefaultSrc,
                                        "default-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::ScriptSrc,
                                        "script-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::StyleSrc,
                                        "style-src 'self';");
  source->OverrideContentSecurityPolicy(Directive::ConnectSrc,
                                        "connect-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::FrameSrc,
                                        "frame-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::ChildSrc,
                                        "child-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::WorkerSrc,
                                        "worker-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::ObjectSrc,
                                        "object-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::ImgSrc, "img-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::FontSrc, "font-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::MediaSrc,
                                        "media-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::ManifestSrc,
                                        "manifest-src 'none';");
  source->OverrideContentSecurityPolicy(Directive::BaseURI, "base-uri 'none';");
  source->OverrideContentSecurityPolicy(Directive::FormAction,
                                        "form-action 'none';");
  source->OverrideContentSecurityPolicy(Directive::FrameAncestors,
                                        "frame-ancestors 'none';");
  source->SetRequestFilter(
      base::BindRepeating([](const std::string&) { return true; }),
      base::BindRepeating(&HandleRequest, PageHtml()));
}

NewTabUI::~NewTabUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(NewTabUI)

}  // namespace mb
