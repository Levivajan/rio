// Copyright 2026 The Rio Authors. All rights reserved.

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/extensions/api/side_panel/side_panel_service.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_id.h"
#include "chrome/browser/ui/side_panel/side_panel_entry_key.h"
#include "chrome/browser/ui/side_panel/side_panel_ui.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/importer/clu_import_service.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "clu/browser/site/clu_site_extension.h"
#include "clu/browser/site/clu_site_key.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_anchor_view.h"
#include "clu/browser/ui/views/frame/clu_layout.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/webui/clu_site_handlers.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_site.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_test_helper.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/common/extension.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The CluExtensions* checks of docs/PLAN.md 12.14's second box and the M7 "Done when" on Chromium: current MV3
// extensions run as in Chrome. The action row lists, pins and runs them over Chromium's own toolbar actions, popups
// hang from the clicked button, chrome.sidePanel opens next to the page card, chrome.tabs.create makes a Clu row,
// options open in a window of their own, typed chrome-extension:// URLs are refused, enable, disable, install prompt
// and removal work. The Web Store installs of uBlock Origin Lite and Bitwarden need the network and run by hand
// (MANUAL_, --run-manual).
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kHelloId[] = "mcckmealkdgpohljofgfdabgihifolmd";
constexpr char kSidePanelId[] = "jipcjgaggonhfhgpaoppmdjbbdllaffd";

base::FilePath FixtureExtension(const char* name) {
  base::FilePath root;
  CHECK(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  return root.AppendASCII("clu")
      .AppendASCII("test")
      .AppendASCII("data")
      .AppendASCII("extensions")
      .AppendASCII(name);
}

class CluExtensionsBrowserTest : public extensions::ExtensionBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    extensions::ExtensionBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    handler_ = std::make_unique<CluExtensionsHandler>(window().GetWeakPtr());
  }

  void TearDownOnMainThread() override {
    CluImportService::Get()->SetImportedExtensionsForTesting({});
    handler_.reset();
    extensions::ExtensionBrowserTest::TearDownOnMainThread();
  }

  // The last sidebar import listed `id`, imported into the Rio profile `into`.
  void ListImported(const std::string& id, const std::string& into) {
    CluImportService::Get()->SetImportedExtensionsForTesting(
        {SidebarExtension{.id = id, .name = "Listed", .profile_id = into}});
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluExtensionsHandler& extensions_handler() { return *handler_; }
  std::string profile_id() { return window().site().profile_id(); }

  const extensions::Extension* Load(const char* name) {
    const extensions::Extension* extension =
        LoadExtension(FixtureExtension(name));
    EXPECT_TRUE(extension);
    return extension;
  }

  // windows[id].actionRow as published, or none.
  base::Value ActionRow() {
    const base::DictValue state = window().CurrentState();
    const base::Value* row = state.FindDict("windows")
                                 ->FindDict(window().window_id())
                                 ->Find("actionRow");
    return row ? row->Clone() : base::Value();
  }

  // The row's entry for extension `id`, or none.
  base::Value Action(const std::string& id) {
    base::Value row = ActionRow();
    if (!row.is_dict()) {
      return base::Value();
    }
    for (base::Value& action : *row.GetDict().FindList("actions")) {
      if (*action.GetDict().FindString("id") == id) {
        return std::move(action);
      }
    }
    return base::Value();
  }

  mojom::CallResultPtr Call(
      base::OnceCallback<void(base::OnceCallback<void(mojom::CallResultPtr)>)>
          call) {
    CallFuture done;
    std::move(call).Run(done.GetCallback());
    return done.Take();
  }

  // Runs `script` in the hello-action fixture's service worker; its chrome.test.sendScriptResult() value.
  base::Value RunInHello(const std::string& script) {
    return extensions::BackgroundScriptExecutor::ExecuteScript(
        profile(), kHelloId, script,
        extensions::BackgroundScriptExecutor::ResultCapture::kSendScriptResult);
  }

  mojom::CallResultPtr RunAction(const std::string& id) {
    return Call(base::BindOnce(
        &CluExtensionsHandler::RunAction, base::Unretained(handler_.get()),
        mojom::ExtensionsRunActionInput::New(
            id, mojom::Rect::New(40, 52, 24, 24))));
  }

  std::unique_ptr<CluExtensionsHandler> handler_;
};

// 12.14: the fixture's action shows in the row and the extensions menu, pins, and its popup opens from the clicked
// button with "Hello from Clu fixture"; the badge reads 7.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsTheRowListsPinsAndRunsTheFixtureAction) {
  EXPECT_TRUE(ActionRow().is_none());
  ASSERT_TRUE(Load("hello-action"));
  ASSERT_TRUE(base::test::RunUntil([&] { return Action(kHelloId).is_dict(); }));
  EXPECT_EQ(*Action(kHelloId).GetDict().FindString("name"), "Clu Fixture");
  // Clu's own component extension never shows.
  EXPECT_TRUE(Action(kCluExtensionId).is_none());

  ASSERT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::SetPinned,
                                  base::Unretained(handler_.get()),
                                  mojom::ExtensionsSetPinnedInput::New(
                                      kHelloId, true)))
                  ->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Action(kHelloId).GetDict().FindBool("pinned") == true;
  }));
  EXPECT_TRUE(ToolbarActionsModel::Get(profile())->IsActionPinned(kHelloId));
  // The icon loads and reaches state as a PNG data URL.
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::Value action = Action(kHelloId);
    const std::string* icon = action.GetDict().FindString("icon");
    return icon && icon->starts_with("data:image/png;base64,");
  }));

  extensions::ExtensionHostTestHelper popup_waiter(profile(), kHelloId);
  popup_waiter.RestrictToType(extensions::mojom::ViewType::kExtensionPopup);
  ASSERT_TRUE(RunAction(kHelloId)->is_value_json());
  extensions::ExtensionHost* popup = popup_waiter.WaitForHostCompletedFirstLoad();
  ASSERT_TRUE(popup);
  EXPECT_NE(content::EvalJs(popup->host_contents(), "document.body.textContent")
                .ExtractString()
                .find("Hello from Clu fixture"),
            std::string::npos);
  // The popup hangs from the button the user clicked, not the page card's corner.
  views::Widget* widget =
      views::Widget::GetWidgetForNativeView(popup->host_contents()->GetNativeView());
  ASSERT_TRUE(widget);
  views::BubbleDialogDelegate* bubble =
      widget->GetTopLevelWidget()->widget_delegate()->AsBubbleDialogDelegate();
  ASSERT_TRUE(bubble);
  EXPECT_EQ(bubble->GetAnchorView(), shell().extension_anchor_view());
  EXPECT_EQ(shell().extension_anchor_view()->bounds(),
            gfx::Rect(40, 52, 24, 24));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::Value action = Action(kHelloId);
    const std::string* badge = action.GetDict().FindString("badge");
    return badge && *badge == "7";
  }));

  ASSERT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::SetPinned,
                                  base::Unretained(handler_.get()),
                                  mojom::ExtensionsSetPinnedInput::New(
                                      kHelloId, false)))
                  ->is_value_json());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Action(kHelloId).GetDict().FindBool("pinned") == false;
  }));
}

// A side panel extension opens on the right. chrome.sidePanel with openPanelOnActionClick: a click on the action opens
// the panel at the page card's trailing edge, and the page keeps the rest.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsSidePanelOpensNextToThePageCard) {
  ASSERT_TRUE(Load("side-panel"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return extensions::SidePanelService::Get(profile())
        ->OpenSidePanelOnIconClick(kSidePanelId);
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return Action(kSidePanelId).is_dict(); }));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  const gfx::Rect card_before = browser_view->multi_contents_view()->bounds();

  ASSERT_TRUE(RunAction(kSidePanelId)->is_value_json());
  SidePanelUI* side_panel_ui = SidePanelUI::From(browser());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return side_panel_ui->IsSidePanelEntryShowing(
        SidePanelEntryKey(SidePanelEntryId::kExtension, kSidePanelId));
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return browser_view->side_panel()->GetVisible() &&
           !browser_view->side_panel()->bounds().IsEmpty();
  }));
  const gfx::Rect panel = browser_view->side_panel()->bounds();
  const gfx::Rect card = browser_view->multi_contents_view()->bounds();
  // On the right of the page, inside the card's old span, the same height; the page got narrower.
  EXPECT_GE(panel.x(), card.right());
  EXPECT_LE(panel.right(), card_before.right());
  EXPECT_EQ(panel.y(), card.y());
  EXPECT_EQ(panel.height(), card.height());
  EXPECT_LT(card.width(), card_before.width());
  EXPECT_EQ(card.x(), card_before.x());

  // A second click closes it (Chromium's toggle), and the page takes the card back.
  ASSERT_TRUE(RunAction(kSidePanelId)->is_value_json());
  EXPECT_TRUE(base::test::RunUntil([&] {
    return !side_panel_ui->IsSidePanelShowing() &&
           browser_view->multi_contents_view()->bounds() == card_before;
  }));
}

// 12.14: chrome.tabs.create from an extension makes a Clu tab: a Today row at the top of the space.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsTabsCreateMakesATodayRow) {
  ASSERT_TRUE(Load("hello-action"));
  const GURL url = embedded_test_server()->GetURL("/tabs/b.html");
  const base::Value id = extensions::BackgroundScriptExecutor::ExecuteScript(
      profile(), kHelloId,
      base::StrCat({"chrome.tabs.create({url: '", url.spec(),
                    "'}).then(t => chrome.test.sendScriptResult(t.id))"}),
      extensions::BackgroundScriptExecutor::ResultCapture::kSendScriptResult);
  ASSERT_TRUE(id.is_int());
  ASSERT_TRUE(base::test::RunUntil([&] {
    CluModelService* model = window().model();
    const std::vector<std::string>& today =
        model->tree().Today(window().space_id());
    if (today.empty()) {
      return false;
    }
    const TabNode* node = model->tree().Get(today.front());
    return node && GURL(node->url) == url;
  }));
}

// Fix pass (docs/chromium/fixes.md, leftovers 4): the window's hidden placeholder page is no tab to extensions.
// chrome.tabs.query, chrome.windows.getAll({populate: true}) and chrome.tabs.onActivated name the rows' pages only, so
// a tab-counting extension counts what the sidebar shows.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsDoNotSeeTheHiddenPlaceholderTab) {
  ASSERT_TRUE(Load("hello-action"));
  ASSERT_TRUE(window()
                  .CreateRow(embedded_test_server()->GetURL("/tabs/b.html"),
                             std::nullopt, /*activate=*/true)
                  .has_value());
  TabStripModel* strip = browser()->tab_strip_model();
  // A window showing a row keeps one placeholder beside it (RemovePlaceholderIfUnused).
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (int i = 0; i < strip->count(); ++i) {
      if (CluPlaceholder::Is(strip->GetWebContentsAt(i))) {
        return true;
      }
    }
    return false;
  }));
  base::ListValue rows;
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!CluPlaceholder::Is(contents)) {
      rows.Append(extensions::ExtensionTabUtil::GetTabId(contents));
    }
  }
  ASSERT_FALSE(rows.empty());
  EXPECT_EQ(RunInHello("chrome.tabs.query({}).then(tabs => "
                       "chrome.test.sendScriptResult(tabs.map(t => t.id)))"),
            base::Value(rows.Clone()));
  EXPECT_EQ(RunInHello("chrome.windows.getAll({populate: true}).then(ws => "
                       "chrome.test.sendScriptResult("
                       "ws.flatMap(w => w.tabs.map(t => t.id))))"),
            base::Value(rows.Clone()));

  ASSERT_EQ(RunInHello("self.cluActivated = [];"
                       "chrome.tabs.onActivated.addListener("
                       "  info => self.cluActivated.push(info.tabId));"
                       "chrome.test.sendScriptResult(true)"),
            base::Value(true));
  // Closing every page the extension sees empties the window: the placeholder becomes its active tab, and the
  // extension sees no tab at all.
  ASSERT_EQ(RunInHello("chrome.tabs.query({})"
                       "  .then(tabs => chrome.tabs.remove(tabs.map(t => t.id)))"
                       "  .then(() => chrome.test.sendScriptResult(true))"),
            base::Value(true));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return CluPlaceholder::Is(strip->GetActiveWebContents());
  }));
  const int placeholder_id =
      extensions::ExtensionTabUtil::GetTabId(strip->GetActiveWebContents());
  EXPECT_EQ(RunInHello("chrome.tabs.query({active: true}).then(tabs => "
                       "chrome.test.sendScriptResult(tabs.length))"),
            base::Value(0));

  // A new tab activates; the events name it, and never the placeholder before it.
  const base::Value created = RunInHello(base::StrCat(
      {"chrome.tabs.create({url: '",
       embedded_test_server()->GetURL("/tabs/a.html").spec(),
       "'}).then(t => chrome.test.sendScriptResult(t.id))"}));
  ASSERT_TRUE(created.is_int());
  base::Value activated;
  ASSERT_TRUE(base::test::RunUntil([&] {
    activated = RunInHello("chrome.test.sendScriptResult(self.cluActivated)");
    return activated.is_list() &&
           activated.GetList().contains(created.GetInt());
  }));
  EXPECT_FALSE(activated.GetList().contains(placeholder_id));
}

// Decision 57: options open in a window of their own (a popup Browser), never a tab of the sidebar.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsOptionsOpenInAWindowOfTheirOwn) {
  ASSERT_TRUE(Load("hello-action"));
  const size_t tabs = browser()->tab_strip_model()->count();
  ASSERT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::OpenOptions,
                                  base::Unretained(handler_.get()),
                                  mojom::ExtensionsOpenOptionsInput::New(
                                      profile_id(), kHelloId)))
                  ->is_value_json());
  BrowserWindowInterface* popup = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (BrowserWindowInterface* b : GetAllBrowserWindowInterfaces()) {
      if (b->GetType() == BrowserWindowInterface::TYPE_POPUP &&
          b->GetTabStripModel()->GetActiveWebContents() &&
          b->GetTabStripModel()->GetActiveWebContents()->GetVisibleURL() ==
              GURL(base::StrCat({"chrome-extension://", kHelloId,
                                 "/options.html"}))) {
        popup = b;
        return true;
      }
    }
    return false;
  }));
  EXPECT_NE(popup, browser());
  EXPECT_EQ(static_cast<size_t>(browser()->tab_strip_model()->count()), tabs);
}

// 12.14: "typed chrome-extension:// is refused".
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsTypedExtensionUrlsAreRefused) {
  ASSERT_TRUE(Load("hello-action"));
  base::expected<std::string, std::string> row = window().CreateRow(
      GURL(base::StrCat({"chrome-extension://", kHelloId, "/popup.html"})),
      std::nullopt, /*activate=*/true);
  ASSERT_FALSE(row.has_value());
  EXPECT_EQ(row.error(), "blocked-url");
}

// 12.14: "enable and disable toggle": a disabled extension leaves the row and comes back.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsEnableAndDisableToggle) {
  ASSERT_TRUE(Load("hello-action"));
  ASSERT_TRUE(base::test::RunUntil([&] { return Action(kHelloId).is_dict(); }));
  auto set_enabled = [&](bool enabled) {
    return Call(base::BindOnce(&CluExtensionsHandler::SetEnabled,
                               base::Unretained(handler_.get()),
                               mojom::ExtensionsSetEnabledInput::New(
                                   profile_id(), kHelloId, enabled)));
  };
  ASSERT_TRUE(set_enabled(false)->is_value_json());
  EXPECT_FALSE(extensions::ExtensionRegistry::Get(profile())
                   ->enabled_extensions()
                   .Contains(kHelloId));
  EXPECT_TRUE(base::test::RunUntil([&] { return ActionRow().is_none(); }));
  // The list still shows it, off.
  mojom::CallResultPtr listed = Call(base::BindOnce(
      &CluExtensionsHandler::List, base::Unretained(handler_.get()),
      mojom::ExtensionsListInput::New(profile_id())));
  base::Value list =
      base::JSONReader::Read(listed->get_value_json(), base::JSON_PARSE_RFC)
          .value();
  ASSERT_EQ(list.GetList().size(), 1u);
  EXPECT_EQ(list.GetList()[0].GetDict().FindBool("enabled"), false);
  ASSERT_TRUE(set_enabled(true)->is_value_json());
  EXPECT_TRUE(base::test::RunUntil([&] { return Action(kHelloId).is_dict(); }));
  // Clu's own extension can be neither listed nor turned off.
  mojom::CallResultPtr own = Call(base::BindOnce(
      &CluExtensionsHandler::SetEnabled, base::Unretained(handler_.get()),
      mojom::ExtensionsSetEnabledInput::New(profile_id(), kCluExtensionId,
                                            false)));
  ASSERT_TRUE(own->is_error());
  EXPECT_EQ(own->get_error()->code, mojom::ErrorCode::kNotFound);
}

// Turning an extension back on goes through Chromium's enable flow: one that asked for new permissions shows the
// re-enable prompt, and only accepting it turns it on; one Chromium disabled as not allowlisted stays off.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsReEnableNeedsChromiumsPrompt) {
  ASSERT_TRUE(Load("hello-action"));
  auto set_enabled = [&](bool enabled) {
    return Call(base::BindOnce(&CluExtensionsHandler::SetEnabled,
                               base::Unretained(handler_.get()),
                               mojom::ExtensionsSetEnabledInput::New(
                                   profile_id(), kHelloId, enabled)));
  };
  auto is_enabled = [&] {
    return extensions::ExtensionRegistry::Get(profile())
        ->enabled_extensions()
        .Contains(kHelloId);
  };
  extensions::ExtensionRegistrar* registrar =
      extensions::ExtensionRegistrar::Get(profile());
  registrar->DisableExtension(
      kHelloId, {extensions::disable_reason::DISABLE_PERMISSIONS_INCREASE});
  ASSERT_FALSE(is_enabled());
  {
    extensions::ScopedTestDialogAutoConfirm decline(
        extensions::ScopedTestDialogAutoConfirm::CANCEL);
    mojom::CallResultPtr declined = set_enabled(true);
    ASSERT_TRUE(declined->is_error());
    EXPECT_EQ(declined->get_error()->code, mojom::ErrorCode::kRejected);
    EXPECT_FALSE(is_enabled());
  }
  {
    extensions::ScopedTestDialogAutoConfirm accept(
        extensions::ScopedTestDialogAutoConfirm::ACCEPT);
    ASSERT_TRUE(set_enabled(true)->is_value_json());
    EXPECT_TRUE(is_enabled());
  }

  registrar->DisableExtension(
      kHelloId, {extensions::disable_reason::DISABLE_NOT_ALLOWLISTED});
  ASSERT_FALSE(is_enabled());
  extensions::ScopedTestDialogAutoConfirm accept(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  ASSERT_TRUE(set_enabled(true)->is_error());
  EXPECT_FALSE(is_enabled());
}

// A popup hangs from the button just clicked for that extension only: another extension's popup (its shortcut,
// chrome.action.openPopup()) and any popup after the layout changed use the window's own anchor.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsAnOldClickAnchorsNoOtherPopup) {
  ASSERT_TRUE(Load("hello-action"));
  ASSERT_TRUE(base::test::RunUntil([&] { return Action(kHelloId).is_dict(); }));
  ASSERT_TRUE(RunAction(kHelloId)->is_value_json());
  EXPECT_EQ(GetExtensionPopupAnchorView(browser(), kHelloId),
            shell().extension_anchor_view());
  EXPECT_EQ(GetExtensionPopupAnchorView(browser(), kSidePanelId),
            shell().anchor_view());
  shell().ToggleSidebar();
  EXPECT_EQ(GetExtensionPopupAnchorView(browser(), kHelloId),
            shell().anchor_view());
}

// Pins belong to the profile: a private window can't change them, even for an extension allowed there.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsAPrivateWindowPinsNothing) {
  ASSERT_TRUE(LoadExtension(FixtureExtension("hello-action"),
                            {.allow_in_incognito = true}));
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile());
  const bool pinned = model->IsActionPinned(kHelloId);
  BrowserWindowInterface* incognito = CreateIncognitoBrowser(profile());
  CluWindowState* private_window = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(private_window);
  CluExtensionsHandler handler(private_window->GetWeakPtr());
  mojom::CallResultPtr result = Call(base::BindOnce(
      &CluExtensionsHandler::SetPinned, base::Unretained(&handler),
      mojom::ExtensionsSetPinnedInput::New(kHelloId, !pinned)));
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->reason, "private-window");
  EXPECT_EQ(model->IsActionPinned(kHelloId), pinned);
}

// 12.14: "install prompt and rio://extensions remove work": a packed CRX installs through Chromium's install prompt
// (accepted here), its action is in the row, and removal takes it out of the row and the profile.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsInstallPromptThenRemove) {
  const base::FilePath crx = PackExtension(FixtureExtension("hello-action"));
  ASSERT_FALSE(crx.empty());
  const extensions::Extension* installed =
      InstallExtensionWithUIAutoConfirm(crx, 1);
  ASSERT_TRUE(installed);
  ASSERT_TRUE(base::test::RunUntil([&] { return Action(installed->id()).is_dict(); }));

  const std::string id = installed->id();
  ASSERT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::Remove,
                                  base::Unretained(handler_.get()),
                                  mojom::ExtensionsRemoveInput::New(
                                      profile_id(), id)))
                  ->is_value_json());
  EXPECT_FALSE(
      extensions::ExtensionRegistry::Get(profile())->GetInstalledExtension(id));
  EXPECT_TRUE(base::test::RunUntil([&] { return ActionRow().is_none(); }));
}

// The import summary's Reinstall. An extension that is already on resolves at once (the row turns to
// Installed) and shows no prompt.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsReinstallOfAnInstalledExtensionIsInstalled) {
  ASSERT_TRUE(Load("hello-action"));
  ListImported(kHelloId, profile_id());
  extensions::ScopedTestDialogAutoConfirm cancel(
      extensions::ScopedTestDialogAutoConfirm::CANCEL);
  EXPECT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::InstallFromStore,
                                  base::Unretained(handler_.get()),
                                  mojom::ExtensionsInstallFromStoreInput::New(
                                      kHelloId)))
                  ->is_value_json());
}

// The store can't be reached here (every host resolves to the test server), so the prompt never opens and
// the call answers "failed" instead of hanging; nothing is installed.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsReinstallAnswersWhenTheStoreFails) {
  constexpr char kStoreOnlyId[] = "ddkjiahejlhfcafbddmgiahcphecmpfh";
  ListImported(kStoreOnlyId, profile_id());
  extensions::ScopedTestDialogAutoConfirm accept(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  mojom::CallResultPtr result = Call(base::BindOnce(
      &CluExtensionsHandler::InstallFromStore, base::Unretained(handler_.get()),
      mojom::ExtensionsInstallFromStoreInput::New(kStoreOnlyId)));
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->reason, "failed");
  EXPECT_FALSE(extensions::ExtensionRegistry::Get(profile())
                   ->GetInstalledExtension(kStoreOnlyId));
}

// A private window installs nothing from the store.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsReinstallRefusesAnIdNoImportListed) {
  ASSERT_TRUE(Load("hello-action"));
  ListImported("ddkjiahejlhfcafbddmgiahcphecmpfh", profile_id());
  extensions::ScopedTestDialogAutoConfirm accept(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  for (const char* id : {kHelloId, "nngceckbapebfimnlniiiahkandclblb"}) {
    mojom::CallResultPtr result = Call(base::BindOnce(
        &CluExtensionsHandler::InstallFromStore,
        base::Unretained(handler_.get()),
        mojom::ExtensionsInstallFromStoreInput::New(id)));
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->get_error()->code, mojom::ErrorCode::kNotFound);
  }
  CluImportService::Get()->SetImportedExtensionsForTesting({});
  mojom::CallResultPtr none = Call(base::BindOnce(
      &CluExtensionsHandler::InstallFromStore, base::Unretained(handler_.get()),
      mojom::ExtensionsInstallFromStoreInput::New(
          "ddkjiahejlhfcafbddmgiahcphecmpfh")));
  ASSERT_TRUE(none->is_error());
  EXPECT_EQ(none->get_error()->code, mojom::ErrorCode::kNotFound);
}

// Reinstall from a private window (the settings window can be bound to one) installs into the
// profile the import went to, never the private one: an extension on there turns to Installed instead of answering
// private-window forever.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsReinstallFromAPrivateWindowUsesTheImportedProfile) {
  ASSERT_TRUE(Load("hello-action"));
  ListImported(kHelloId, profile_id());
  BrowserWindowInterface* incognito = CreateIncognitoBrowser(profile());
  CluWindowState* private_window = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(private_window);
  CluExtensionsHandler handler(private_window->GetWeakPtr());
  extensions::ScopedTestDialogAutoConfirm cancel(
      extensions::ScopedTestDialogAutoConfirm::CANCEL);
  EXPECT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::InstallFromStore,
                                  base::Unretained(&handler),
                                  mojom::ExtensionsInstallFromStoreInput::New(
                                      kHelloId)))
                  ->is_value_json());
}

// The extension goes to the profile its source profile became, not the calling window's. The
// calling window's profile has it on, the import's profile doesn't, so the call doesn't answer Installed at once: it
// goes to the store for the import's profile (unreachable here, so "failed"), and that profile stays without it.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsReinstallTargetsTheImportedProfile) {
  ASSERT_TRUE(Load("hello-action"));
  CluSpacesService* spaces = CluSpacesService::Get();
  base::test::TestFuture<base::expected<std::string, SpacesError>> made;
  spaces->CreateProfile(std::string("Imported"), made.GetCallback());
  ASSERT_TRUE(made.Get().has_value());
  const std::string imported_id = made.Get().value();
  base::test::TestFuture<Profile*> loaded;
  spaces->LoadProfile(imported_id, loaded.GetCallback());
  ASSERT_TRUE(loaded.Get());
  ASSERT_NE(loaded.Get(), profile());
  ListImported(kHelloId, imported_id);
  extensions::ScopedTestDialogAutoConfirm accept(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  mojom::CallResultPtr result = Call(base::BindOnce(
      &CluExtensionsHandler::InstallFromStore, base::Unretained(handler_.get()),
      mojom::ExtensionsInstallFromStoreInput::New(kHelloId)));
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->reason, "failed");
  EXPECT_FALSE(extensions::ExtensionRegistry::Get(loaded.Get())
                   ->GetInstalledExtension(kHelloId));
}

// The Developer Mode row in Site controls turns Developer Mode on for the page's site. The tab publishes
// devMode, the page card moves under the strip's band, and the choice is stored per site in local state. Local sites
// start on with no stored row; turning one off stores false. The strip's console button opens DevTools on the page.
IN_PROC_BROWSER_TEST_F(CluExtensionsBrowserTest,
                       CluExtensionsDeveloperModeTurnsOnPerSiteAndShowsTheStrip) {
  CluSiteControlsHandler site_controls(window().GetWeakPtr());
  auto active = [&] {
    return browser()->tab_strip_model()->GetActiveWebContents();
  };
  // The window's front row as published, and its devMode.
  auto front = [&] {
    const base::DictValue state = window().CurrentState();
    const std::string* id = state.FindDict("windows")
                                ->FindDict(window().window_id())
                                ->FindString("activeTabId");
    return id ? *id : std::string();
  };
  auto dev_mode = [&] {
    const base::DictValue state = window().CurrentState();
    const base::DictValue* tab = state.FindDict("tabs")->FindDict(front());
    return tab && tab->FindBool("devMode") == true;
  };
  auto set = [&](bool on) {
    return Call(base::BindOnce(
        &CluSiteControlsHandler::SetDevMode, base::Unretained(&site_controls),
        mojom::SiteControlsSetDevModeInput::New(front(), on)));
  };
  const int band_top = kCardInset + kToolbarHeight;

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/tabs/a.html")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const base::DictValue state = window().CurrentState();
    return !front().empty() &&
           state.FindDict("tabs")->FindDict(front()) != nullptr;
  }));
  EXPECT_FALSE(dev_mode());
  EXPECT_FALSE(shell().chrome_state().dev_strip);
  EXPECT_EQ(shell().layout().page_rect.y(), kCardInset);

  ASSERT_TRUE(set(true)->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return dev_mode() && shell().chrome_state().dev_strip;
  }));
  EXPECT_EQ(shell().layout().page_rect.y(), band_top);
  EXPECT_EQ(g_browser_process->local_state()
                ->GetDict(kCluDevModeSitesPref)
                .FindBool("a.test"),
            true);

  // The strip's console button: DevTools opens on the page.
  ASSERT_TRUE(Call(base::BindOnce(&CluSiteControlsHandler::DevTool,
                                  base::Unretained(&site_controls),
                                  mojom::SiteControlsDevToolInput::New(
                                      mojom::SiteControlsDevToolInputTool::
                                          kConsole)))
                  ->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return DevToolsWindow::GetInstanceForInspectedWebContents(active()) !=
           nullptr;
  }));
  DevToolsWindowTesting::CloseDevToolsWindowSync(
      DevToolsWindow::GetInstanceForInspectedWebContents(active()));

  // A local site starts on with no stored row; off stores false and the band goes.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("localhost", "/tabs/b.html")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return dev_mode() && shell().chrome_state().dev_strip;
  }));
  EXPECT_FALSE(g_browser_process->local_state()
                   ->GetDict(kCluDevModeSitesPref)
                   .contains("localhost"));
  ASSERT_TRUE(set(false)->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !dev_mode() && !shell().chrome_state().dev_strip;
  }));
  EXPECT_EQ(shell().layout().page_rect.y(), kCardInset);
  EXPECT_EQ(g_browser_process->local_state()
                ->GetDict(kCluDevModeSitesPref)
                .FindBool("localhost"),
            false);

  // Turning a.test off again drops its row: off is its default.
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/tabs/a.html")));
  ASSERT_TRUE(base::test::RunUntil([&] { return dev_mode(); }));
  ASSERT_TRUE(set(false)->is_value_json());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !shell().chrome_state().dev_strip;
  }));
  EXPECT_FALSE(g_browser_process->local_state()
                   ->GetDict(kCluDevModeSitesPref)
                   .contains("a.test"));
}

// The M7 "Done when": real Chrome Web Store installs of uBlock Origin Lite and Bitwarden (current MV3) work end to end:
// the store page's own button, Chromium's install prompt (accepted), the CRX from Google's update servers, the action
// in Clu's row, the popup from it. Needs the network and never signs in: run by hand with --run-manual.
class CluExtensionsWebStoreBrowserTest : public CluExtensionsBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    CluExtensionsBrowserTest::SetUpOnMainThread();
    host_resolver()->AllowDirectLookup("*");
  }

  void InstallFromStore(const char* id) {
    extensions::ScopedTestDialogAutoConfirm accept(
        extensions::ScopedTestDialogAutoConfirm::ACCEPT);
    extensions::TestExtensionRegistryObserver installed(
        extensions::ExtensionRegistry::Get(profile()), id);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), GURL(base::StrCat(
                       {"https://chromewebstore.google.com/detail/", id}))));
    content::WebContents* store =
        browser()->tab_strip_model()->GetActiveWebContents();
    constexpr char kAdd[] = R"(
      new Promise(resolve => {
        const find = () => [...document.querySelectorAll('button')]
            .find(b => /^\s*Add to /i.test(b.textContent));
        const tick = () => { const b = find(); if (b) { b.click(); resolve(true); }
                             else setTimeout(tick, 250); };
        tick();
      }))";
    ASSERT_EQ(true, content::EvalJs(store, kAdd));
    scoped_refptr<const extensions::Extension> extension =
        installed.WaitForExtensionInstalled();
    ASSERT_TRUE(extension);
    EXPECT_EQ(extension->manifest_version(), 3);
    ASSERT_TRUE(base::test::RunUntil([&] { return Action(id).is_dict(); }));
    extensions::ExtensionHostTestHelper popup_waiter(profile(), id);
    popup_waiter.RestrictToType(extensions::mojom::ViewType::kExtensionPopup);
    ASSERT_TRUE(RunAction(id)->is_value_json());
    EXPECT_TRUE(popup_waiter.WaitForHostCompletedFirstLoad());
  }
};

IN_PROC_BROWSER_TEST_F(CluExtensionsWebStoreBrowserTest,
                       MANUAL_CluExtensionsWebStoreInstallsUBlockOriginLiteAndBitwarden) {
  InstallFromStore("ddkjiahejlhfcafbddmgiahcphecmpfh");  // uBlock Origin Lite
  InstallFromStore("nngceckbapebfimnlniiiahkandclblb");  // Bitwarden
}

// With the network: Reinstall shows Chromium's Web Store install prompt (accepted here) and resolves once
// uBlock Origin Lite is installed; a second click resolves at once.
IN_PROC_BROWSER_TEST_F(CluExtensionsWebStoreBrowserTest,
                       MANUAL_CluExtensionsReinstallInstallsFromTheStorePrompt) {
  constexpr char kUBlockLite[] = "ddkjiahejlhfcafbddmgiahcphecmpfh";
  ListImported(kUBlockLite, profile_id());
  extensions::ScopedTestDialogAutoConfirm accept(
      extensions::ScopedTestDialogAutoConfirm::ACCEPT);
  for (int click = 0; click < 2; ++click) {
    EXPECT_TRUE(Call(base::BindOnce(&CluExtensionsHandler::InstallFromStore,
                                    base::Unretained(handler_.get()),
                                    mojom::ExtensionsInstallFromStoreInput::New(
                                        kUBlockLite)))
                    ->is_value_json());
    EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                    ->enabled_extensions()
                    .Contains(kUBlockLite));
  }
}

}  // namespace
}  // namespace clu
