#include "mb/ui/new_tab/new_tab_ui.h"

#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_url_handler.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/common/extension.h"
#include "extensions/test/test_extension_dir.h"
#include "mb/generated/branding/branding.h"
#include "net/http/http_response_headers.h"
#include "ui/base/l10n/l10n_util.h"

namespace mb {
namespace {

using MbNewTabBrowserTest = InProcessBrowserTest;

class MainFrameCspObserver : public content::WebContentsObserver {
 public:
  explicit MainFrameCspObserver(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}

  MainFrameCspObserver(const MainFrameCspObserver&) = delete;
  MainFrameCspObserver& operator=(const MainFrameCspObserver&) = delete;
  ~MainFrameCspObserver() override = default;

  const std::optional<std::string>& header() const { return header_; }

 private:
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted() ||
        handle->IsSameDocument() || handle->IsErrorPage()) {
      return;
    }
    const net::HttpResponseHeaders* headers = handle->GetResponseHeaders();
    if (headers) {
      header_ = headers->GetNormalizedHeader("Content-Security-Policy");
    }
  }

  std::optional<std::string> header_;
};

IN_PROC_BROWSER_TEST_F(MbNewTabBrowserTest, BuiltInOriginsUseLocalProductPage) {
  for (const GURL& url : {chrome::ChromeUINewTabURLAsGURL(),
                          chrome::ChromeUINewTabPageURLAsGURL(),
                          GURL(chrome::kChromeUINewTabPageThirdPartyURL)}) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(contents->GetWebUI());
    EXPECT_TRUE(contents->GetWebUI()->GetController()->GetAs<NewTabUI>());
    EXPECT_EQ(
        branding::kFullName,
        content::EvalJs(contents, "document.querySelector('h1').textContent"));
    EXPECT_EQ(0, content::EvalJs(
                     contents,
                     "document.querySelectorAll('script,iframe,object,embed')"
                     ".length"));
    EXPECT_EQ(1, content::EvalJs(contents, "document.styleSheets.length"));
    EXPECT_EQ(true, content::EvalJs(
                        contents,
                        "new URL(document.styleSheets[0].href).origin === "
                        "location.origin"));
  }
}

IN_PROC_BROWSER_TEST_F(MbNewTabBrowserTest, RemoteNtpPreferenceDoesNotRewrite) {
  browser()->GetProfile()->GetPrefs()->SetString(
      prefs::kNewTabPageLocationOverride, "https://ntp.invalid/managed");
  GURL url = chrome::ChromeUINewTabURLAsGURL();
  content::BrowserURLHandler::GetInstance()->RewriteURLIfNecessary(
      &url, browser()->GetProfile());
  EXPECT_EQ(chrome::ChromeUINewTabPageURLAsGURL(), url);
}

IN_PROC_BROWSER_TEST_F(MbNewTabBrowserTest,
                       RemoteSearchProviderKeepsSelectionWhileLocalNtpWins) {
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(service);
  search_test_utils::WaitForTemplateURLServiceToLoad(service);

  TemplateURLData data;
  data.SetShortName(u"Non-Google remote provider");
  data.SetKeyword(u"mb-local-test");
  data.SetURL("https://search.example.invalid/?q={searchTerms}");
  data.new_tab_url = "https://ntp.example.invalid/new-tab";
  TemplateURL* provider = service->Add(std::make_unique<TemplateURL>(data));
  service->SetUserSelectedDefaultSearchProvider(provider);
  ASSERT_EQ(provider, service->GetDefaultSearchProvider());
  ASSERT_EQ("https://ntp.example.invalid/new-tab", provider->new_tab_url());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           chrome::ChromeUINewTabURLAsGURL()));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents->GetWebUI());
  EXPECT_TRUE(contents->GetWebUI()->GetController()->GetAs<NewTabUI>());
  EXPECT_EQ(provider, service->GetDefaultSearchProvider());
}

IN_PROC_BROWSER_TEST_F(MbNewTabBrowserTest,
                       ExtensionNewTabOverrideAndDisableRestoreLocalNtp) {
  extensions::TestExtensionDir extension_dir;
  extension_dir.WriteManifest(R"({
    "name": "Local new tab override",
    "version": "1.0",
    "manifest_version": 3,
    "chrome_url_overrides": { "newtab": "newtab.html" }
  })");
  extension_dir.WriteFile(FILE_PATH_LITERAL("newtab.html"),
                          "<!doctype html><title>Extension NTP</title>"
                          "<p id=extension-ntp>extension new tab</p>");

  extensions::ChromeTestExtensionLoader loader(browser()->GetProfile());
  scoped_refptr<const extensions::Extension> extension =
      loader.LoadExtension(extension_dir.UnpackedPath());
  ASSERT_TRUE(extension);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           chrome::ChromeUINewTabURLAsGURL()));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  // NTP aliases intentionally keep WebContents' reported URL at chrome://newtab.
  // The committed navigation entry retains the actual extension destination,
  // as checked by Chromium's ExtensionOverrideTest::ExtensionControlsPage.
  auto* entry = contents->GetController().GetLastCommittedEntry();
  ASSERT_TRUE(entry);
  EXPECT_EQ(extension->GetResourceURL("newtab.html"), entry->GetURL());
  EXPECT_EQ(
      "extension new tab",
      content::EvalJs(contents,
                      "document.querySelector('#extension-ntp').textContent"));

  extensions::ExtensionRegistrar::Get(browser()->GetProfile())
      ->DisableExtension(extension->id(),
                         {extensions::disable_reason::DISABLE_USER_ACTION});

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           chrome::ChromeUINewTabURLAsGURL()));
  contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents->GetWebUI());
  EXPECT_TRUE(contents->GetWebUI()->GetController()->GetAs<NewTabUI>());
}

IN_PROC_BROWSER_TEST_F(MbNewTabBrowserTest,
                       CspBlocksDataModuleImportWithoutNetworkRequest) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  MainFrameCspObserver csp_observer(contents);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           chrome::ChromeUINewTabURLAsGURL()));
  ASSERT_TRUE(contents->GetWebUI());
  ASSERT_TRUE(contents->GetWebUI()->GetController()->GetAs<NewTabUI>());

  ASSERT_TRUE(csp_observer.header());
  EXPECT_NE(std::string::npos,
            csp_observer.header()->find("script-src 'none';"));

  // Dynamic import is a module fetch, not the Trusted-Types-gated
  // HTMLScriptElement.src sink. The data: module needs no server or network
  // request. The CSP response header and its console violation establish why
  // the import rejects.
  content::WebContentsConsoleObserver console_observer(contents);
  console_observer.SetPattern(
      "*violates the following Content Security Policy directive:*"
      "script-src 'none'*");
  EXPECT_EQ("blocked", content::EvalJs(contents, R"(
    import('data:text/javascript,globalThis.mbDataModuleRan=1')
        .then(() => 'loaded', () => 'blocked')
  )"));
  ASSERT_TRUE(console_observer.Wait());
  EXPECT_EQ(false,
            content::EvalJs(contents,
                            "Object.hasOwn(globalThis, 'mbDataModuleRan')"));
}

IN_PROC_BROWSER_TEST_F(MbNewTabBrowserTest, IncognitoKeepsNativeNewTabPage) {
  Browser* incognito = CreateIncognitoBrowser();
  GURL url = chrome::ChromeUINewTabURLAsGURL();
  content::BrowserURLHandler::GetInstance()->RewriteURLIfNecessary(
      &url, incognito->GetProfile());
  EXPECT_EQ(chrome::ChromeUINewTabURLAsGURL(), url);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, url));
  auto* contents = incognito->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents->GetWebUI());
  EXPECT_TRUE(contents->GetBrowserContext()->IsOffTheRecord());
  EXPECT_EQ(browser()->GetProfile(), incognito->GetProfile()->GetOriginalProfile());
  // The legacy native incognito controller has no GetAs() type metadata.
  // Verify its actual privacy-page markup and OTR context without downcasting.
  EXPECT_EQ(l10n_util::GetStringUTF8(IDS_NEW_TAB_OTR_TITLE),
            content::EvalJs(contents, "document.querySelector('h1').textContent"));
  for (const char* selector : {"#incognitothemecss", "#bulletpoints-wrapper",
                                "#cookie-controls"}) {
    EXPECT_EQ(true, content::EvalJs(contents, content::JsReplace(
                           "!!document.querySelector($1)", selector)))
        << selector;
  }
  // GRIT flattenhtml inlines incognito_tab.css into the native document;
  // its original source <link> is deliberately absent at runtime.
}

}  // namespace
}  // namespace mb
