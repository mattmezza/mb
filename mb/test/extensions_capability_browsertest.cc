// SPDX-License-Identifier: BSD-3-Clause

#include <string>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/extension_action_test_helper.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_test_helper.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "extensions/test/extension_background_page_waiter.h"
#include "extensions/test/extension_test_message_listener.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace extensions {
namespace {

class MbExtensionCapabilityBrowserTest : public ExtensionBrowserTest {
 protected:
  base::FilePath SmokeFixturePath() const {
    return base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
        .AppendASCII("mb")
        .AppendASCII("test")
        .AppendASCII("extensions")
        .AppendASCII("smoke");
  }

  const Extension* LoadSmokeFixture() {
    const Extension* extension = LoadExtension(SmokeFixturePath());
    EXPECT_TRUE(extension);
    if (!extension) {
      return nullptr;
    }

    std::string reason;
    EXPECT_TRUE(ExtensionBackgroundPageWaiter::CanWaitFor(*extension, reason))
        << reason;
    ExtensionBackgroundPageWaiter(profile(), *extension)
        .WaitForBackgroundInitialized();
    return extension;
  }

  void NavigateToLoopbackAndWait(const std::string& expected_message,
                                 const Extension& extension) {
    ExtensionTestMessageListener listener(expected_message);
    listener.set_extension_id(extension.id());
    ASSERT_TRUE(NavigateToURL(GetActiveWebContents(),
                              embedded_test_server()->GetURL("/title1.html")));
    ASSERT_TRUE(listener.WaitUntilSatisfied());
  }

  bool FixtureStatusIsPresent() {
    return content::EvalJs(GetActiveWebContents(),
                           "Boolean(document.getElementById('browser-"
                           "capability-fixture-status'))")
        .ExtractBool();
  }

  std::string FixtureStatusText() {
    return content::EvalJs(
               GetActiveWebContents(),
               "document.getElementById('browser-capability-fixture-status')"
               ".textContent")
        .ExtractString();
  }

  std::string PopupStateText(content::WebContents* popup_contents) {
    return content::EvalJs(popup_contents,
                           "document.querySelector('#state').textContent")
        .ExtractString();
  }
};

IN_PROC_BROWSER_TEST_F(MbExtensionCapabilityBrowserTest,
                       UnpackedFixtureAndExtensionsWebUi) {
  const Extension* extension = LoadSmokeFixture();
  ASSERT_TRUE(extension);
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(extension->id()));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL(chrome::kChromeUIExtensionsURL)));
  EXPECT_EQ(GURL(chrome::kChromeUIExtensionsURL),
            GetActiveWebContents()->GetLastCommittedURL());
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(extension->id()));
}

IN_PROC_BROWSER_TEST_F(MbExtensionCapabilityBrowserTest,
                       ContentStorageAndActionPopupRoundTrip) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const Extension* extension = LoadSmokeFixture();
  ASSERT_TRUE(extension);

  NavigateToLoopbackAndWait("content:1", *extension);
  EXPECT_TRUE(FixtureStatusIsPresent());
  EXPECT_EQ(FixtureStatusText(),
            "Browser capability fixture: regular context; service worker "
            "responded; local count 1.");

  ExtensionHostTestHelper popup_host(profile(), extension->id());
  popup_host.RestrictToType(mojom::ViewType::kExtensionPopup);
  ExtensionTestMessageListener popup_state("popup:state:1:not saved");
  popup_state.set_extension_id(extension->id());

  auto action_helper = ExtensionActionTestHelper::Create(browser());
  action_helper->WaitForExtensionsContainerLayout();
  ASSERT_TRUE(action_helper->HasAction(extension->id()));
  action_helper->Press(extension->id());
  ExtensionHost* host = popup_host.WaitForHostCompletedFirstLoad();
  ASSERT_TRUE(host);
  ASSERT_TRUE(popup_state.WaitUntilSatisfied());

  content::WebContents* popup_contents = host->host_contents();
  ASSERT_TRUE(popup_contents);
  EXPECT_EQ(PopupStateText(popup_contents),
            "Last loopback context: regular. Local content count: 1. Local "
            "popup ping count: 0. Local fixture value: not saved.");
  ExtensionTestMessageListener ping("popup:ping:1");
  ping.set_extension_id(extension->id());
  ASSERT_TRUE(content::ExecJs(popup_contents,
                              "document.querySelector('#ping').click();"));
  ASSERT_TRUE(ping.WaitUntilSatisfied());
  EXPECT_EQ(PopupStateText(popup_contents),
            "Last loopback context: regular. Local content count: 1. Local "
            "popup ping count: 1. Local fixture value: not saved.");

  ExtensionTestMessageListener persisted("popup:state:1:saved");
  persisted.set_extension_id(extension->id());
  ASSERT_TRUE(content::ExecJs(popup_contents,
                              "document.querySelector('#save').click();"));
  ASSERT_TRUE(persisted.WaitUntilSatisfied());
  EXPECT_EQ(PopupStateText(popup_contents),
            "Last loopback context: regular. Local content count: 1. Local "
            "popup ping count: 1. Local fixture value: saved.");
  EXPECT_TRUE(action_helper->HidePopup());
}

IN_PROC_BROWSER_TEST_F(MbExtensionCapabilityBrowserTest,
                       DisableEnableAndRemoveUnpackedFixture) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const Extension* extension = LoadSmokeFixture();
  ASSERT_TRUE(extension);
  const ExtensionId id = extension->id();

  NavigateToLoopbackAndWait("content:1", *extension);
  TestExtensionRegistryObserver disabled(extension_registry(), id);
  DisableExtension(id);
  ASSERT_TRUE(disabled.WaitForExtensionUnloaded());
  EXPECT_FALSE(extension_registry()->enabled_extensions().Contains(id));
  EXPECT_TRUE(extension_registry()->disabled_extensions().Contains(id));
  ASSERT_TRUE(NavigateToURL(GetActiveWebContents(),
                            embedded_test_server()->GetURL("/title2.html")));
  EXPECT_FALSE(FixtureStatusIsPresent());

  TestExtensionRegistryObserver enabled(extension_registry(), id);
  EnableExtension(id);
  const Extension* reenabled = enabled.WaitForExtensionLoaded().get();
  ASSERT_TRUE(reenabled);
  ExtensionBackgroundPageWaiter(profile(), *reenabled)
      .WaitForBackgroundInitialized();
  NavigateToLoopbackAndWait("content:2", *reenabled);
  EXPECT_TRUE(FixtureStatusIsPresent());

  TestExtensionRegistryObserver uninstalled(extension_registry(), id);
  UninstallExtension(id);
  ASSERT_TRUE(uninstalled.WaitForExtensionUninstalled());
  EXPECT_FALSE(extension_registry()->GetInstalledExtension(id));
  ASSERT_TRUE(NavigateToURL(GetActiveWebContents(),
                            embedded_test_server()->GetURL("/title3.html")));
  EXPECT_FALSE(FixtureStatusIsPresent());
}

}  // namespace
}  // namespace extensions
