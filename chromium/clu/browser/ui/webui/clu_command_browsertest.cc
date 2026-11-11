// Copyright 2026 The Rio Authors. All rights reserved.

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "clu/browser/command/clu_history_index.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_command_bar.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/common/clu_urls.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/url_row.h"
#include "components/history/core/common/pref_names.h"
#include "components/history/core/test/history_service_test_util.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_mouse_event.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "ui/base/ui_base_types.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

// The CluCommand* checks of the M5 "Done when" row (docs/chromium/PLAN.md section 6): docs/PLAN.md 12.8 on Chromium,
// against the real overlay WebUI, Chromium's HistoryService, TemplateURLService and AutocompleteController. The
// boxes each test covers are in docs/chromium/acceptance-map.md.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

constexpr char kWaitForNoBar[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector('[data-surface="command"]')
                            ? setTimeout(check, 20) : resolve(true);
    check();
  }))";

constexpr char kWaitForInputFocus[] = R"(
  new Promise(resolve => {
    const check = () => {
      const input = document.querySelector('[data-surface="command"] input');
      input && document.activeElement === input ? resolve(true)
                                                : setTimeout(check, 20);
    };
    check();
  }))";

// The input event the bar's onChange reads as one typed character at the end (inline completion stays allowed).
constexpr char kType[] = R"(
  (() => {
    const input = document.querySelector('[data-surface="command"] input');
    input.focus();
    for (const ch of Array.from($1)) {
      const setter = Object.getOwnPropertyDescriptor(
          HTMLInputElement.prototype, 'value').set;
      const typed = input.value.slice(0, input.selectionStart ?? input.value.length) + ch;
      setter.call(input, typed);
      input.setSelectionRange(typed.length, typed.length);
      input.dispatchEvent(new InputEvent('input',
          {bubbles: true, inputType: 'insertText', data: ch}));
    }
    return document.activeElement === input;
  })())";

// The rows the bar lists, as `kind|group|title`.
constexpr char kRows[] = R"(
  Array.from(document.querySelectorAll('[data-surface="command"] [role=option]'))
      .map(r => [r.dataset.kind, r.dataset.group,
                 r.querySelector('.clu-cmd-title')?.textContent ?? ''].join('|')))";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

base::Value ValueOf(const mojom::CallResultPtr& result) {
  EXPECT_TRUE(IsOk(result));
  if (!IsOk(result)) {
    return base::Value();
  }
  return base::JSONReader::Read(result->get_value_json(), base::JSON_PARSE_RFC)
      .value_or(base::Value());
}

// The fixture engine: results pages echo the query, and the
// suggest endpoint answers OpenSearch JSON with junk around three good completions.
std::unique_ptr<net::test_server::HttpResponse> HandleEngine(
    const net::test_server::HttpRequest& request) {
  const GURL url = request.GetURL();
  std::string q;
  for (const std::string& pair : base::SplitString(
           url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (pair.starts_with("q=")) {
      q = base::UnescapeURLComponent(
          pair.substr(2),
          base::UnescapeRule::SPACES | base::UnescapeRule::PATH_SEPARATORS |
              base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
              base::UnescapeRule::REPLACE_PLUS_WITH_SPACE);
    }
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  if (url.path() == "/engine/search") {
    response->set_content_type("text/html; charset=utf-8");
    response->set_content(base::StrCat(
        {"<!doctype html><title>Results</title><h1 id=q>",
         base::EscapeForHTML(q), "</h1>"}));
    return response;
  }
  if (url.path() == "/engine/suggest") {
    response->set_content_type("application/x-suggestions+json");
    base::ListValue items;
    for (const char* tail : {" one", " one", " two", "", " three", " four",
                             " five", " six", " seven"}) {
      items.Append(q + tail);
    }
    items.Append(std::string(400, 'x'));
    base::ListValue body;
    body.Append(q);
    body.Append(std::move(items));
    std::string json;
    base::JSONWriter::Write(body, &json);
    response->set_content(json);
    return response;
  }
  return nullptr;
}

class CluCommandBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleEngine));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(CluShell::From(browser()));
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    ASSERT_TRUE(content::WaitForLoadStop(shell().overlay_contents()));
    ui::TestClipboard::CreateForCurrentThread();
    handler_ = std::make_unique<CluCommandHandler>(window().GetWeakPtr(),
                                                   browser()->GetProfile());
  }

  void TearDownOnMainThread() override {
    handler_.reset();
    ui::Clipboard::DestroyClipboardForCurrentThread();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  TabStripModel& strip() { return *browser()->GetTabStripModel(); }
  CluCommandHandler& handler() { return *handler_; }
  content::WebContents* overlay() { return shell().overlay_contents(); }

  GURL Url(const std::string& path) {
    return embedded_test_server()->GetURL(path);
  }

  // A Today row on `path`, shown and loaded.
  std::string Open(const std::string& path) {
    base::expected<std::string, std::string> id =
        window().CreateRow(Url(path), std::nullopt, /*activate=*/true);
    EXPECT_TRUE(id.has_value());
    if (!id.has_value()) {
      return std::string();
    }
    EXPECT_TRUE(content::WaitForLoadStop(Contents(id.value())));
    return id.value();
  }

  content::WebContents* Contents(const std::string& row_id) {
    tabs::TabInterface* tab = window().FindTab(row_id);
    return tab ? tab->GetContents() : nullptr;
  }

  bool HasFocus(content::WebContents* contents) {
    return contents && contents->GetRenderWidgetHostView() &&
           contents->GetRenderWidgetHostView()->HasFocus();
  }

  // A chord from the keyboard into `contents`: down, char, up through its RenderWidgetHost, which asks the browser
  // first (PreHandleKeyboardEvent) as for a key from the OS. content::SimulateKeyPress() marks its events
  // skip_if_unhandled, which skips the browser's pre-handling, so it cannot test browser keys.
  void Key(content::WebContents* contents,
           ui::DomKey key,
           ui::DomCode code,
           ui::KeyboardCode key_code,
           bool command,
           bool shift = false) {
    const int modifiers = (command ? blink::WebInputEvent::kMetaKey : 0) |
                          (shift ? blink::WebInputEvent::kShiftKey : 0);
    content::RenderWidgetHost* widget =
        contents->GetPrimaryMainFrame()->GetRenderWidgetHost();
    for (blink::WebInputEvent::Type type :
         {blink::WebInputEvent::Type::kRawKeyDown,
          blink::WebInputEvent::Type::kChar,
          blink::WebInputEvent::Type::kKeyUp}) {
      input::NativeWebKeyboardEvent event(type, modifiers,
                                          base::TimeTicks::Now());
      event.dom_key = key;
      event.dom_code = static_cast<int>(code);
      event.windows_key_code = key_code;
      if (type != blink::WebInputEvent::Type::kKeyUp && key.IsCharacter()) {
        event.text[0] = key.ToCharacter();
        event.unmodified_text[0] = key.ToCharacter();
      }
      widget->ForwardKeyboardEvent(event);
    }
  }

  // Opens the bar (Cmd+T) and waits for its input to take focus, which the bar gives it once the session arrives.
  // A bar that just closed is waited out first, so the input found is the new session's.
  std::string OpenBar() {
    if (!window().command_session()) {
      EXPECT_EQ(true, content::EvalJs(overlay(), kWaitForNoBar));
    }
    EXPECT_TRUE(window().RunCommand("commandbar.newTab"));
    EXPECT_TRUE(window().command_session());
    EXPECT_EQ(true, content::EvalJs(overlay(), content::JsReplace(
                                                   kWaitForSelector,
                                                   "[data-surface=\"command\"] "
                                                   "input")));
    EXPECT_EQ(true, content::EvalJs(overlay(), kWaitForInputFocus));
    return window().command_session() ? window().command_session()->session_id
                                      : std::string();
  }

  void Type(const std::string& text) {
    EXPECT_EQ(true,
              content::EvalJs(overlay(), content::JsReplace(kType, text)));
  }

  // The bar's rows once `predicate` holds for them.
  std::vector<std::string> RowsWhen(
      base::RepeatingCallback<bool(const std::vector<std::string>&)>
          predicate) {
    std::vector<std::string> rows;
    EXPECT_TRUE(base::test::RunUntil([&] {
      rows.clear();
      for (const base::Value& row :
           content::EvalJs(overlay(), kRows).ExtractList()) {
        rows.push_back(row.GetString());
      }
      return predicate.Run(rows);
    })) << "the bar listed: " << base::JoinString(rows, ", ");
    return rows;
  }

  mojom::CallResultPtr Execute(const std::string& session,
                               mojom::CommandResultPtr result,
                               mojom::CommandExecuteInputDisposition
                                   disposition =
                                       mojom::CommandExecuteInputDisposition::
                                           kDefault) {
    CallFuture done;
    handler().Execute(mojom::CommandExecuteInput::New(
                          session, std::move(result), disposition, ""),
                      done.GetCallback());
    return done.Take();
  }

  std::string ExecuteError(const mojom::CallResultPtr& result) {
    const base::Value value = ValueOf(result);
    const std::string* error =
        value.is_dict() ? value.GetDict().FindString("error") : nullptr;
    return error ? *error : std::string();
  }

  // The fixture engine as the profile's default search engine, with suggestions.
  void UseFixtureEngine() {
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
    search_test_utils::WaitForTemplateURLServiceToLoad(service);
    TemplateURLData data;
    data.SetShortName(u"Fixture");
    data.SetKeyword(u"fixture.test");
    data.SetURL(Url("/engine/search?q={searchTerms}").spec());
    data.suggestions_url = Url("/engine/suggest?q={searchTerms}").spec();
    TemplateURL* engine = service->Add(std::make_unique<TemplateURL>(data));
    ASSERT_TRUE(engine);
    service->SetUserSelectedDefaultSearchProvider(engine);
  }

  history::HistoryService* history() {
    return HistoryServiceFactory::GetForProfile(
        browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  }

  base::DictValue Sync(double since_rev) {
    CallFuture synced;
    handler().SyncIndex(mojom::CommandSyncIndexInput::New(since_rev),
                        synced.GetCallback());
    base::Value value = ValueOf(synced.Take());
    return value.is_dict() ? std::move(value).TakeDict() : base::DictValue();
  }

  // Closes every prompt the top overlay shows (the space editor, a confirm).
  void ClosePrompts() {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* surfaces =
        state.FindDict("windows")
            ->FindDict(window().window_id())
            ->FindListByDottedPath("overlay.surfaces");
    if (!surfaces) {
      return;
    }
    std::vector<std::string> open;
    for (const base::Value& surface : *surfaces) {
      const std::string* kind = surface.GetDict().FindString("surface");
      const std::string* instance = surface.GetDict().FindString("instanceId");
      if (kind && instance && *kind != "command" && *kind != "toast") {
        open.push_back(*instance);
      }
    }
    for (const std::string& instance : open) {
      window().CloseOverlayInstance(instance);
    }
  }

  std::unique_ptr<CluCommandHandler> handler_;
};

bool Contains(const std::vector<std::string>& rows, std::string_view part) {
  return std::ranges::any_of(rows, [&](const std::string& row) {
    return row.find(part) != std::string::npos;
  });
}

// 12.8 box 1 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandCmdTAndCmdLOpenFromAFocusedPageAndToggle) {
  const std::string row = Open("/spaces/keylog.html");
  content::WebContents* page = Contents(row);
  page->Focus();

  // Cmd+T from the focused page opens the bar in new-tab mode; the page never sees the key.
  Key(page, ui::DomKey::FromCharacter('t'), ui::DomCode::US_T, ui::VKEY_T,
      /*command=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window().command_session() && shell().IsOverlayVisible();
  }));
  EXPECT_EQ(window().command_session()->mode, "new-tab");
  const std::string session = window().command_session()->session_id;

  // Cmd+L, the other key, switches the open session in place to edit-url, prefilled with the page URL.
  Key(overlay(), ui::DomKey::FromCharacter('l'), ui::DomCode::US_L, ui::VKEY_L,
      /*command=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window().command_session() &&
           window().command_session()->mode == "edit-url";
  }));
  EXPECT_EQ(window().command_session()->session_id, session);
  EXPECT_EQ(window().command_session()->initial_text,
            Url("/spaces/keylog.html").spec());

  // The same key again closes it, and the page has focus again.
  Key(overlay(), ui::DomKey::FromCharacter('l'), ui::DomCode::US_L, ui::VKEY_L,
      /*command=*/true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().command_session(); }));
  EXPECT_FALSE(shell().IsOverlayVisible());
  EXPECT_TRUE(base::test::RunUntil([&] { return HasFocus(page); }));
  EXPECT_EQ(content::EvalJs(page, "window.__keys.join(',')"), "");

  // Esc closes and refocuses the page.
  OpenBar();
  content::SimulateKeyPress(overlay(), ui::DomKey::ESCAPE, ui::DomCode::ESCAPE,
                            ui::VKEY_ESCAPE, false, false, false, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().command_session(); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return HasFocus(page); }));

  // A click on the scrim (anywhere outside the bar) closes and refocuses the page.
  OpenBar();
  const gfx::Rect card = shell().layout().page_rect;
  content::SimulateMouseClickAt(
      overlay(), 0, blink::WebMouseEvent::Button::kLeft,
      gfx::Point(card.x() + 16, card.bottom() - 16));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().command_session(); }));
  EXPECT_TRUE(base::test::RunUntil([&] { return HasFocus(page); }));
  EXPECT_EQ(content::EvalJs(page, "window.__keys.join(',')"), "");
}

// 12.8 box 2 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandTheBrowserChecksEveryRowAgain) {
  UseFixtureEngine();
  Open("/command/harbor.html");
  const int pages = strip().count();
  const std::string session = OpenBar();

  // The overlay's classification is never trusted: script and data URLs never load.
  EXPECT_EQ(ExecuteError(Execute(
                session, mojom::CommandResult::NewNavigate(
                             mojom::CommandResultNavigate::New(
                                 "navigate", "javascript:alert(1)")))),
            "blocked-url");
  EXPECT_EQ(ExecuteError(Execute(
                session, mojom::CommandResult::NewNavigate(
                             mojom::CommandResultNavigate::New(
                                 "navigate", "data:text/html,<b>x</b>")))),
            "blocked-url");
  // History rows are web pages only.
  EXPECT_EQ(ExecuteError(Execute(
                session, mojom::CommandResult::NewHistory(
                             mojom::CommandResultHistory::New(
                                 "history", "file:///etc/passwd", "x")))),
            "blocked-url");
  // A palette id that is no command, and a provider token that is no row.
  EXPECT_EQ(ExecuteError(Execute(session, mojom::CommandResult::NewCommand(
                                              mojom::CommandResultCommand::New(
                                                  "command", "no.such")))),
            "invalid");
  EXPECT_EQ(ExecuteError(Execute(session,
                                 mojom::CommandResult::NewProvided(
                                     mojom::CommandResultProvided::New(
                                         "provided", "spaces", "not-a-space")))),
            "invalid");
  // A stale session runs nothing.
  EXPECT_EQ(ExecuteError(Execute(
                "00000000-0000-4000-8000-000000000001",
                mojom::CommandResult::NewNavigate(
                    mojom::CommandResultNavigate::New(
                        "navigate", Url("/command/inbox.html").spec())))),
            "stale");
  EXPECT_EQ(strip().count(), pages);

  // `file:///etc/passwd` typed in the bar is a search (classifyInput); the browser runs it on the profile's engine.
  window().CloseCommandBar();
  const std::string search_session = OpenBar();
  content::TestNavigationObserver results(
      Url("/engine/search?q=file%3A%2F%2F%2Fetc%2Fpasswd"));
  results.StartWatchingNewWebContents();
  ASSERT_TRUE(IsOk(Execute(search_session,
                           mojom::CommandResult::NewSearch(
                               mojom::CommandResultSearch::New(
                                   "search", "google", "file:///etc/passwd")))));
  results.Wait();
  EXPECT_FALSE(window().command_session());
  EXPECT_EQ(content::EvalJs(strip().GetActiveWebContents(),
                            "document.getElementById('q').textContent"),
            "file:///etc/passwd");
}

// 12.8 box 3 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandAStrongTabMatchSwitchesAndALooseOneSearches) {
  UseFixtureEngine();
  const std::string inbox = Open("/command/inbox.html");
  Open("/command/harbor.html");
  const int pages = strip().count();

  // "gmail" matches the open Inbox tab by a word prefix: the top row switches to it.
  OpenBar();
  Type("gmail");
  const std::vector<std::string> rows =
      RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
        return !rows.empty() && rows[0].starts_with("tab|top|");
      }));
  EXPECT_TRUE(rows[0].find("Inbox") != std::string::npos) << rows[0];
  // Tab beats history: the same page is not listed again as a history row.
  EXPECT_FALSE(Contains(rows, "history|history|Inbox"));
  content::SimulateKeyPress(overlay(), ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, false, false, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().command_session(); }));
  EXPECT_EQ(window().ActiveRowId(), inbox);
  EXPECT_EQ(strip().count(), pages);

  // "gm" is only a loose match: the top row searches, the tab is one row down.
  OpenBar();
  Type("gm");
  const std::vector<std::string> loose =
      RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
        return !rows.empty() && rows[0].starts_with("search|top|");
      }));
  EXPECT_FALSE(loose[0].find("Inbox") != std::string::npos);
  window().CloseCommandBar();

  // Inline completion: "harb" completes to the open tab's host with the added text selected; Backspace takes only
  // the completion away (useCommandBar.test.ts covers IME composition).
  OpenBar();
  Type("127.0");
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(overlay(), R"(
      (() => {
        const i = document.querySelector('[data-surface="command"] input');
        return i.value.length > 5 && i.selectionStart === 5 &&
               i.selectionEnd === i.value.length;
      })())")
        .ExtractBool();
  }));
  content::SimulateKeyPress(overlay(), ui::DomKey::BACKSPACE,
                            ui::DomCode::BACKSPACE, ui::VKEY_BACK, false, false,
                            false, false);
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(
               overlay(),
               "document.querySelector('[data-surface=\"command\"] input')"
               ".value")
               .ExtractString() == "127.0";
  }));
}

// 12.8 box 4 ---------------------------------------------------------------------------------------------------------

// Palette commands the harbor page's state hides: Separate needs a split, Turn on the blocker needs it off for the
// site, Toggle Site tweaks needs a tweak (M6 and M7), Check for Updates needs an updater, which a test build lacks
// (M12; CluUpdateBrowserTest covers the row with one), Previous Space needs a space before the first
// (CluCommandSpaceStepsNeedASpaceThatWay).
constexpr std::string_view kHiddenHere[] = {
    "split.separate", "blocking.turnOn", "tweaks.toggle", "platform.checkForUpdates",
    "spaces.prev",
};

// Commands the run below skips because they put up a native dialog or window that a browser test cannot dismiss (Reveal
// Rio Data opens Finder), or reload the page later (Clear Cookies and Refresh, Clear Cache and Refresh, which
// CluCommandClearSiteDataAndRefresh runs); the test still checks they are listed and enabled, and CluKeys* checks their
// chords.
constexpr std::string_view kListedOnly[] = {
    "page.print", "page.savePage", "platform.openFile", "browsingData.open",
    "page.devtools", "window.new", "window.newPrivate", "platform.showLogs",
    "page.clearCookiesAndRefresh", "page.clearCacheAndRefresh",
};

// Fix pass (docs/chromium/fixes.md, leftovers 3): Next and Previous Space are listed only when a space lies that way
// (they never wrap), so a window with one space lists neither. The chord stays reserved for the window either way.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandSpaceStepsNeedASpaceThatWay) {
  Open("/command/harbor.html");
  const auto listed = [&] {
    OpenBar();
    std::set<std::string> ids;
    for (const base::Value& value : handler().ActionRows()) {
      if (const std::string* id = value.GetDict().FindString("id");
          id && !value.GetDict().FindString("providerId")) {
        ids.insert(*id);
      }
    }
    window().CloseCommandBar();
    return ids;
  };
  ASSERT_EQ(CluSpacesService::Get()->list().spaces().size(), 1u);
  std::set<std::string> ids = listed();
  EXPECT_FALSE(ids.contains("spaces.next"));
  EXPECT_FALSE(ids.contains("spaces.prev"));
  EXPECT_TRUE(ids.contains("spaces.newSpace"));
  // The chord still belongs to the window: it runs, and does nothing.
  const std::string only = window().space_id();
  EXPECT_TRUE(window().RunKeyCommand("spaces.next"));
  EXPECT_EQ(window().space_id(), only);

  ASSERT_TRUE(space_actions::CreateSpace(&window(), std::string("Two"),
                                         std::nullopt, std::nullopt,
                                         std::nullopt)
                  .has_value());
  const std::vector<std::string> order =
      CluSpacesService::Get()->list().Order();
  ASSERT_EQ(order.size(), 2u);
  ASSERT_TRUE(window().ShowSpace(order[0]));
  ids = listed();
  EXPECT_TRUE(ids.contains("spaces.next"));
  EXPECT_FALSE(ids.contains("spaces.prev"));
  ASSERT_TRUE(window().ShowSpace(order[1]));
  ids = listed();
  EXPECT_FALSE(ids.contains("spaces.next"));
  EXPECT_TRUE(ids.contains("spaces.prev"));
}

// A new space is lavender (indigo) by default and goes right after the window's space.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandNewSpaceIsLavenderAfterTheCurrentOne) {
  const std::string home = window().space_id();
  base::expected<std::string, SpacesError> two = space_actions::CreateSpace(
      &window(), std::string("Two"), std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(two.has_value());
  EXPECT_EQ(window().space_id(), two.value());
  EXPECT_EQ(CluSpacesService::Get()->list().FindSpace(two.value())->color,
            "indigo");
  ASSERT_TRUE(window().ShowSpace(home));
  base::expected<std::string, SpacesError> three = space_actions::CreateSpace(
      &window(), std::string("Three"), std::nullopt, std::nullopt,
      std::nullopt);
  ASSERT_TRUE(three.has_value());
  EXPECT_EQ(CluSpacesService::Get()->list().Order(),
            (std::vector<std::string>{home, three.value(), two.value()}));
  // A chosen color stays.
  base::expected<std::string, SpacesError> four = space_actions::CreateSpace(
      &window(), std::string("Four"), std::nullopt, std::string("pink"),
      std::nullopt);
  ASSERT_TRUE(four.has_value());
  EXPECT_EQ(CluSpacesService::Get()->list().FindSpace(four.value())->color,
            "pink");
  EXPECT_EQ(CluSpacesService::Get()->list().Order(),
            (std::vector<std::string>{home, three.value(), four.value(),
                                      two.value()}));
}

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandEveryPaletteCommandAndProviderRowRuns) {
  UseFixtureEngine();
  const std::string row = Open("/command/harbor.html");
  // A second space, so the spaces and move-tab providers have rows.
  base::expected<std::string, SpacesError> two = space_actions::CreateSpace(
      &window(), std::string("Two"), std::nullopt, std::nullopt, std::nullopt);
  ASSERT_TRUE(two.has_value());
  ASSERT_TRUE(window().ShowSpace(CluSpacesService::Get()->list().spaces()[0].id));
  ASSERT_TRUE(window().ActivateRow(row));
  // The page zoomed in once, so Actual Size has something to do (Chromium enables IDC_ZOOM_NORMAL only away from the
  // default zoom). The walk below runs zoom.in, zoom.out and zoom.reset in that order, each with something to do.
  ASSERT_TRUE(window().RunCommand("zoom.in"));

  OpenBar();
  const base::ListValue rows = handler().ActionRows();
  std::set<std::string> listed;
  std::set<std::string> providers;
  for (const base::Value& value : rows) {
    const base::DictValue& r = value.GetDict();
    ASSERT_TRUE(r.FindString("id"));
    ASSERT_TRUE(r.FindString("title"));
    EXPECT_FALSE(r.FindString("title")->empty());
    if (const std::string* provider = r.FindString("providerId")) {
      providers.insert(*provider);
      EXPECT_TRUE(r.FindString("token"));
    } else {
      listed.insert(*r.FindString("id"));
    }
  }
  // Every palette command renders, except those the page's state hides.
  for (const CommandSpec& spec : CommandTable()) {
    if (!spec.palette || spec.variant == CommandVariant::kMini) {
      continue;
    }
    const bool later = std::ranges::contains(kHiddenHere, std::string_view(spec.id));
    EXPECT_EQ(listed.contains(spec.id), !later) << spec.id;
  }
  EXPECT_TRUE(providers.contains(CluCommandHandler::kSpacesProvider));
  EXPECT_TRUE(providers.contains(CluCommandHandler::kMoveTabProvider));
  EXPECT_TRUE(providers.contains(CluCommandHandler::kEngineProvider));
  window().CloseCommandBar();

  // Every listed command runs from the bar: the bar closes and the command ran. Each one starts from the same place:
  // the first space, a live row shown, no prompt open.
  const std::string first = CluSpacesService::Get()->list().spaces()[0].id;
  std::string current = row;
  for (const std::string& id : listed) {
    if (std::ranges::contains(kListedOnly, std::string_view(id))) {
      continue;
    }
    ClosePrompts();
    if (window().space_id() != first) {
      ASSERT_TRUE(window().ShowSpace(first));
    }
    if (!window().FindTab(current)) {
      current = Open("/command/harbor.html");
    }
    ASSERT_TRUE(window().ActivateRow(current));
    const std::string session = OpenBar();
    mojom::CallResultPtr result = Execute(
        session, mojom::CommandResult::NewCommand(
                     mojom::CommandResultCommand::New("command", id)));
    EXPECT_TRUE(IsOk(result)) << id;
    EXPECT_EQ(ExecuteError(result), "") << id;
    if (id == "split.add") {
      // Add Split View reopens the bar in split-add mode (docs/chromium/m6.md).
      ASSERT_TRUE(window().command_session()) << id;
      EXPECT_EQ(window().command_session()->mode, "split-add");
      window().CloseCommandBar();
      continue;
    }
    EXPECT_FALSE(window().command_session()) << id;
  }
  ClosePrompts();
  if (window().space_id() != first) {
    ASSERT_TRUE(window().ShowSpace(first));
  }

  // The engine provider sets the profile's engine: the next search goes there.
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(browser()->GetProfile());
  const TemplateURL* fixture = service->GetDefaultSearchProvider();
  std::string other_token;
  std::vector<std::string> current_tokens;
  for (const base::Value& value : handler().ActionRows()) {
    const base::DictValue& r = value.GetDict();
    const std::string* provider = r.FindString("providerId");
    if (!provider || *provider != CluCommandHandler::kEngineProvider) {
      continue;
    }
    const std::string* subtitle = r.FindString("subtitle");
    if (subtitle) {
      EXPECT_EQ(*subtitle, "Current");
      current_tokens.push_back(*r.FindString("token"));
    } else if (other_token.empty()) {
      other_token = *r.FindString("token");
    }
  }
  // The current engine is listed once, marked Current.
  EXPECT_EQ(current_tokens,
            std::vector<std::string>{base::NumberToString(fixture->id().value())});
  ASSERT_FALSE(other_token.empty());
  std::string session = OpenBar();
  ASSERT_TRUE(IsOk(Execute(session, mojom::CommandResult::NewProvided(
                                        mojom::CommandResultProvided::New(
                                            "provided",
                                            CluCommandHandler::kEngineProvider,
                                            other_token)))));
  EXPECT_NE(service->GetDefaultSearchProvider(), fixture);
  EXPECT_EQ(base::NumberToString(service->GetDefaultSearchProvider()->id().value()),
            other_token);

  // "Switch to Two" switches the window.
  session = OpenBar();
  ASSERT_TRUE(IsOk(Execute(
      session, mojom::CommandResult::NewProvided(
                   mojom::CommandResultProvided::New(
                       "provided", CluCommandHandler::kSpacesProvider,
                       two.value())))));
  EXPECT_TRUE(
      base::test::RunUntil([&] { return window().space_id() == two.value(); }));
}

// While a Clear can still be undone, its tabs are off the Today list,
// and the bar offers them neither as Recent tabs nor as tabs to switch to; Undo brings them back to both.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandBarLeavesOutTabsAClearCanStillUndo) {
  Open("/command/harbor.html");
  Open("/command/inbox.html");
  Open("/command/loads.html");
  OpenBar();
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return Contains(rows, "tab|recent|Harbor Lighthouse Notes") &&
           Contains(rows, "tab|recent|Inbox (3) - Gmail");
  }));
  window().CloseCommandBar();

  ASSERT_GE(window().ClearToday(std::nullopt), 3);
  window().Flush();
  OpenBar();
  Type("Harbor");
  // History still knows the page, so the bar lists it from there, never as a tab to switch to.
  const std::vector<std::string> rows =
      RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
        return std::ranges::any_of(rows, [](const std::string& row) {
          return row.starts_with("history|") &&
                 row.ends_with("|Harbor Lighthouse Notes");
        });
      }));
  for (const std::string& row : rows) {
    EXPECT_FALSE(row.starts_with("tab|")) << row;
  }
  window().CloseCommandBar();

  ASSERT_TRUE(window().RunCommand("archive.undoClear"));
  window().Flush();
  OpenBar();
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return Contains(rows, "tab|recent|Harbor Lighthouse Notes");
  }));
}

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandTabOnAnEmptyInputFiltersToActions) {
  OpenBar();
  content::SimulateKeyPress(overlay(), ui::DomKey::TAB, ui::DomCode::TAB,
                            ui::VKEY_TAB, false, false, false, false);
  EXPECT_EQ(true, content::EvalJs(overlay(),
                                  content::JsReplace(kWaitForSelector,
                                                     "[data-actions-only]")));
  const std::vector<std::string> rows =
      RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
        return rows.size() >= 5;
      }));
  for (const std::string& row : rows) {
    EXPECT_TRUE(row.starts_with("command|actions|") ||
                row.starts_with("provided|actions|"))
        << row;
  }
  // Typing narrows the actions; Enter runs the exact one ("Toggle Sidebar").
  Type("Toggle Sidebar");
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return !rows.empty() && rows[0] == "command|actions|Toggle Sidebar";
  }));
  content::SimulateKeyPress(overlay(), ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, false, false, false);
  EXPECT_TRUE(base::test::RunUntil(
      [&] { return shell().chrome_state().sidebar_hidden; }));
}

// 12.8 box 5 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandSuggestionsComeFromTheProfileEngine) {
  UseFixtureEngine();
  Open("/command/harbor.html");
  const std::string session = OpenBar();

  // The fixture engine's strings, cleaned and capped at six.
  CallFuture first;
  CallFuture second;
  handler().Suggest(mojom::CommandSuggestInput::New(session, 1, "fixture"),
                    first.GetCallback());
  // A newer request aborts the older one: it answers at once with nothing.
  handler().Suggest(mojom::CommandSuggestInput::New(session, 2, "harbor"),
                    second.GetCallback());
  const base::Value aborted = ValueOf(first.Take());
  EXPECT_EQ(aborted.GetDict().FindDouble("requestId"), 1);
  EXPECT_TRUE(aborted.GetDict().FindList("items")->empty());
  const base::Value answered = ValueOf(second.Take());
  EXPECT_EQ(answered.GetDict().FindDouble("requestId"), 2);
  std::vector<std::string> items;
  for (const base::Value& item : *answered.GetDict().FindList("items")) {
    items.push_back(item.GetString());
  }
  EXPECT_LE(items.size(), 6u);
  EXPECT_TRUE(std::ranges::contains(items, "harbor one"));
  EXPECT_TRUE(std::ranges::contains(items, "harbor two"));
  EXPECT_FALSE(std::ranges::contains(items, std::string(400, 'x')));
  EXPECT_EQ(std::set<std::string>(items.begin(), items.end()).size(),
            items.size());

  // Input that must not leave the machine asks nothing.
  for (const char* query :
       {"https://example.com", "user:pw@host.test", "someone@example.com"}) {
    CallFuture none;
    handler().Suggest(mojom::CommandSuggestInput::New(session, 3, query),
                      none.GetCallback());
    EXPECT_TRUE(ValueOf(none.Take()).GetDict().FindList("items")->empty())
        << query;
  }

  // The engine is the profile's: a typed query searches it, and the bar labels the row with its name.
  EXPECT_EQ(window().command_session()->engine_name, "Fixture");
  content::TestNavigationObserver results(
      Url("/engine/search?q=fixture+query"));
  results.StartWatchingNewWebContents();
  Type("fixture query");
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    // The row reads the query; Enter goes to the fixture engine below.
    return !rows.empty() && rows[0] == "search|top|fixture query";
  }));
  content::SimulateKeyPress(overlay(), ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, false, false, false);
  results.Wait();
}

// 12.8 box 6 and the history rows ------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandHistoryRowsSyncAsDeltasAndDelete) {
  history::HistoryService* service = history();
  ui_test_utils::WaitForHistoryToLoad(service);
  Open("/command/inbox.html");
  Open("/command/harbor.html");
  history::BlockUntilHistoryProcessesPendingRequests(service);

  const base::DictValue full = Sync(0);
  EXPECT_EQ(full.FindBool("full"), true);
  const double rev = full.FindDouble("rev").value_or(0);
  std::set<std::string> urls;
  for (const base::Value& row : *full.FindList("upserts")) {
    urls.insert(*row.GetDict().FindString("url"));
    EXPECT_EQ(*row.GetDict().FindString("kind"), "history");
  }
  EXPECT_TRUE(urls.contains(Url("/command/inbox.html").spec()));
  EXPECT_TRUE(urls.contains(Url("/command/harbor.html").spec()));

  // A client at the latest rev gets a delta, never the full index again.
  const base::DictValue same = Sync(rev);
  EXPECT_EQ(same.FindBool("full"), false);
  EXPECT_TRUE(same.FindList("deletes")->empty());

  // command.deleteEntry drops the page from history; the next sync says so.
  CallFuture deleted;
  handler().DeleteEntry(
      mojom::CommandDeleteEntryInput::New(Url("/command/inbox.html").spec()),
      deleted.GetCallback());
  ASSERT_TRUE(IsOk(deleted.Take()));
  history::BlockUntilHistoryProcessesPendingRequests(service);
  const base::DictValue after = Sync(rev);
  EXPECT_EQ(after.FindBool("full"), false);
  std::vector<std::string> gone;
  for (const base::Value& key : *after.FindList("deletes")) {
    gone.push_back(key.GetString());
  }
  EXPECT_TRUE(std::ranges::contains(gone, Url("/command/inbox.html").spec()));

  // Junk: a URL that is no web page is refused, with nothing deleted.
  CallFuture junk;
  handler().DeleteEntry(mojom::CommandDeleteEntryInput::New("chrome://history"),
                        junk.GetCallback());
  EXPECT_TRUE(junk.Get()->is_error());

  // The AllowDeletingBrowserHistory policy off: refused, and the page stays in history.
  browser()->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kAllowDeletingBrowserHistory, false);
  CallFuture forbidden;
  handler().DeleteEntry(
      mojom::CommandDeleteEntryInput::New(Url("/command/harbor.html").spec()),
      forbidden.GetCallback());
  ASSERT_TRUE(forbidden.Get()->is_error());
  EXPECT_EQ(forbidden.Get()->get_error()->code, mojom::ErrorCode::kNotAllowed);
  history::BlockUntilHistoryProcessesPendingRequests(service);
  const base::DictValue kept = Sync(rev);
  for (const base::Value& key : *kept.FindList("deletes")) {
    EXPECT_NE(key.GetString(), Url("/command/harbor.html").spec());
  }
}

// A sync that arrives while a read is running (the bar reopened at once, a second window's bar) waits for that read:
// the previous read lacks the visit that started it.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandASyncDuringAReadGetsThatRead) {
  history::HistoryService* service = history();
  ui_test_utils::WaitForHistoryToLoad(service);
  CluHistoryIndex* index = CluHistoryIndex::GetForProfile(browser()->GetProfile());
  base::test::TestFuture<IndexDelta> before;
  index->Sync(0, before.GetCallback());
  ASSERT_TRUE(before.Wait());

  const GURL visited = Url("/command/harbor.html");
  service->AddPage(visited, base::Time::Now(), history::SOURCE_BROWSED);
  history::BlockUntilHistoryProcessesPendingRequests(service);
  base::test::TestFuture<IndexDelta> first;
  base::test::TestFuture<IndexDelta> second;
  index->Sync(0, first.GetCallback());
  index->Sync(0, second.GetCallback());
  auto has_visit = [&](const IndexDelta& delta) {
    return std::ranges::any_of(delta.upserts, [&](const IndexRow& row) {
      return row.url == visited.spec();
    });
  };
  EXPECT_TRUE(has_visit(first.Get()));
  EXPECT_TRUE(has_visit(second.Get()));
}

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandKeystrokesStayWithinBudgetOnTenThousandRows) {
  history::HistoryService* service = history();
  ui_test_utils::WaitForHistoryToLoad(service);
  history::URLRows rows;
  const base::Time now = base::Time::Now();
  for (int i = 0; i < 10000; ++i) {
    history::URLRow row(GURL(base::StrCat(
        {"https://site", base::NumberToString(i % 997), ".test/page/",
         base::NumberToString(i)})));
    row.set_title(base::UTF8ToUTF16(
        base::StrCat({"Page ", base::NumberToString(i), " about harbors"})));
    row.set_visit_count(1 + i % 7);
    row.set_typed_count(i % 3);
    row.set_last_visit(now - base::Minutes(i));
    rows.push_back(std::move(row));
  }
  service->AddPagesWithDetails(rows, history::SOURCE_BROWSED);
  history::BlockUntilHistoryProcessesPendingRequests(service);
  CluHistoryIndex::GetForProfile(browser()->GetProfile())->MarkDirtyForTesting();
  ASSERT_GE(Sync(0).FindList("upserts")->size(), 9000u);

  // Open the bar, let its history sync land, then type 20 characters one by one.
  OpenBar();
  ASSERT_TRUE(content::ExecJs(overlay(),
                              "performance.clearMeasures('cmd:keystroke')"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    Type("h");
    const bool has_history =
        content::EvalJs(overlay(), kRows).ExtractList().size() > 3;
    EXPECT_TRUE(content::ExecJs(overlay(), R"(
      (() => {
        const i = document.querySelector('[data-surface="command"] input');
        const setter = Object.getOwnPropertyDescriptor(
            HTMLInputElement.prototype, 'value').set;
        setter.call(i, '');
        i.dispatchEvent(new InputEvent('input', {bubbles: true}));
      })())"));
    return has_history;
  }));
  ASSERT_TRUE(content::ExecJs(overlay(),
                              "performance.clearMeasures('cmd:keystroke')"));
  for (char c : std::string("harbors page about 1")) {
    Type(std::string(1, c));
    // A fast typist's pace, 40 ms a key: each key
    // gets its own frame and lands at its own point in the frame. Starting each key right after a frame, as two
    // requestAnimationFrame callbacks would, makes every measure wait out nearly a whole frame for vsync.
    ASSERT_TRUE(content::EvalJs(overlay(),
                                "new Promise(r => setTimeout(() => r(true), 40))")
                    .ExtractBool());
  }
  const base::ListValue durations = content::EvalJs(overlay(), R"(
      performance.getEntriesByName('cmd:keystroke', 'measure')
          .map(m => m.duration).sort((a, b) => a - b))")
                                    .ExtractList()
                                    .Clone();
  ASSERT_GE(durations.size(), 15u);
  // Nearest-rank p95.
  const size_t count = durations.size();
  const size_t p95 = std::min(
      count - 1, static_cast<size_t>(std::ceil(count * 0.95)) - 1);
  // One 60 Hz frame plus 1 ms for a loaded Mac; Rio's own work is 4 to 9 ms a key.
  EXPECT_LE(durations[p95].GetDouble(), 17.0) << durations.DebugString();
}

// A tail suggestion searches its whole text, not the tail its contents show; another engine's suggestion (keyword
// mode) never reads as the default engine's.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandSuggestionsAreTheDefaultEnginesWholeText) {
  AutocompleteMatch tail;
  tail.type = omnibox::AutocompleteMatchType::kSearchSuggestTail;
  tail.keyword = u"fixture.test";
  tail.contents = u"\u2026 login";
  tail.fill_into_edit = u"map login";
  tail.search_terms_args =
      std::make_unique<TemplateURLRef::SearchTermsArgs>(u"map login");
  EXPECT_EQ(SuggestionText(tail, u"fixture.test"), u"map login");

  AutocompleteMatch other;
  other.type = omnibox::AutocompleteMatchType::kSearchSuggest;
  other.keyword = u"bing.com";
  other.contents = u"harbor freight";
  EXPECT_EQ(SuggestionText(other, u"fixture.test"), std::nullopt);

  AutocompleteMatch typed;
  typed.type = omnibox::AutocompleteMatchType::kSearchWhatYouTyped;
  typed.keyword = u"fixture.test";
  EXPECT_EQ(SuggestionText(typed, u"fixture.test"), std::nullopt);
}

// Copy URL (04-command F9) -------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandCopyUrlStripsTrackingAndSaysWhenThereIsNone) {
  ui::Clipboard* clipboard = ui::Clipboard::GetForCurrentThread();
  auto read = [&] {
    return base::UTF16ToUTF8(ui::clipboard_test_util::ReadText(
        clipboard, ui::ClipboardBuffer::kCopyPaste, nullptr));
  };
  const std::string row =
      Open("/command/inbox.html?utm_source=x&id=1&fbclid=y#top");
  ASSERT_TRUE(window().RunCommand("edit.copyUrl"));
  EXPECT_EQ(read(), Url("/command/inbox.html?id=1#top").spec());

  ASSERT_TRUE(window().RunCommand("edit.copyUrlMarkdown"));
  EXPECT_EQ(read(), base::StrCat({"[Inbox (3) - Gmail](",
                                  Url("/command/inbox.html?id=1#top").spec(),
                                  ")"}));

  // The handler's own answer, for a row given by id.
  CallFuture copied;
  handler().CopyUrl(mojom::CommandCopyUrlInput::New(row, std::nullopt),
                    copied.GetCallback());
  EXPECT_EQ(*ValueOf(copied.Take()).GetDict().FindString("copied"),
            Url("/command/inbox.html?id=1#top").spec());

  // about:blank has nothing to share: the clipboard keeps what it had.
  ASSERT_TRUE(content::NavigateToURL(Contents(row), GURL("about:blank")));
  CallFuture none;
  handler().CopyUrl(mojom::CommandCopyUrlInput::New(std::nullopt, std::nullopt),
                    none.GetCallback());
  EXPECT_TRUE(ValueOf(none.Take()).GetDict().Find("copied")->is_none());
  EXPECT_EQ(read(), Url("/command/inbox.html?id=1#top").spec());
}

// The copy toast carries a share button; siteControls.share needs a web page.
// The picker itself is macOS's and is not opened here.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandCopyToastSharesAndShareNeedsAWebPage) {
  auto last_toast = [&] {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* toasts = state.FindDict("windows")
                                        ->FindDict(window().window_id())
                                        ->FindList("toasts");
    return toasts && !toasts->empty() ? toasts->back().GetDict().Clone()
                                      : base::DictValue();
  };
  const std::string row = Open("/command/inbox.html");
  ASSERT_TRUE(window().RunCommand("edit.copyUrl"));
  base::DictValue toast = last_toast();
  ASSERT_TRUE(toast.FindString("text"));
  EXPECT_EQ(*toast.FindString("text"), "Copied Current URL");
  ASSERT_TRUE(toast.FindStringByDottedPath("action.label"));
  EXPECT_EQ(*toast.FindStringByDottedPath("action.label"), "Share");
  EXPECT_EQ(*toast.FindStringByDottedPath("action.commandId"),
            kShareToastAction);

  ASSERT_TRUE(content::NavigateToURL(Contents(row), GURL("about:blank")));
  window().ShareUrl(std::nullopt);
  toast = last_toast();
  ASSERT_TRUE(toast.FindString("text"));
  EXPECT_EQ(*toast.FindString("text"), "No URL to share");
  EXPECT_FALSE(toast.FindDict("action"));
}

// The Screenshot tile starts Chromium's own region capture on the window's page.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandScreenshotStartsChromiumsRegionCapture) {
  Open("/command/inbox.html");
  EXPECT_TRUE(chrome::IsCommandEnabled(window().browser(),
                                       IDC_SHARING_HUB_SCREENSHOT));
  EXPECT_TRUE(window().TakeScreenshot());
}

// File ▸ Capture… ⇧⌘2 runs the same region capture as the Screenshot tile: Chromium's own command.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandFileCaptureRunsChromiumsRegionCapture) {
  Open("/command/inbox.html");
  EXPECT_EQ(ChromeCommandFor("page.capture"), IDC_SHARING_HUB_SCREENSHOT);
  EXPECT_TRUE(window().CanRunCommand("page.capture"));
  EXPECT_TRUE(window().RunCommand("page.capture"));
}

// The Capture text tile starts Chromium's region selection on the window's page; a Rio page has no text
// to capture and says so.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandCaptureTextStartsTheRegionSelection) {
  Open("/command/inbox.html");
  EXPECT_TRUE(window().CaptureText());
  EXPECT_TRUE(window().IsCapturingText());
  // A second click restarts it rather than stacking two.
  EXPECT_TRUE(window().CaptureText());
  EXPECT_TRUE(window().IsCapturingText());
}

#if BUILDFLAG(IS_MAC)
// The selected region's text, as macOS reads it, goes to the clipboard with a toast; a region without text toasts
// "No text found" and leaves the clipboard alone.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandCaptureTextCopiesTheTextInTheRegion) {
  Open("/command/inbox.html");
  auto last_toast = [&] {
    const base::DictValue state = window().CurrentState();
    const base::ListValue* toasts = state.FindDict("windows")
                                        ->FindDict(window().window_id())
                                        ->FindList("toasts");
    return toasts && !toasts->empty() && toasts->back().GetDict().FindString("text")
               ? *toasts->back().GetDict().FindString("text")
               : std::string();
  };
  auto clipboard = [] {
    return base::UTF16ToUTF8(ui::clipboard_test_util::ReadText(
        ui::Clipboard::GetForCurrentThread(), ui::ClipboardBuffer::kCopyPaste,
        nullptr));
  };
  auto region = [](std::u16string_view text) {
    gfx::Canvas canvas(gfx::Size(640, 120), 1.0f, /*is_opaque=*/true);
    canvas.DrawColor(SK_ColorWHITE);
    if (!text.empty()) {
      canvas.DrawStringRect(text, gfx::FontList("Helvetica, 56px"),
                            SK_ColorBLACK, gfx::Rect(20, 20, 600, 80));
    }
    return image_editor::ScreenshotCaptureResult{
        gfx::Image::CreateFrom1xBitmap(canvas.GetBitmap()),
        gfx::Rect(0, 0, 640, 120),
        image_editor::ScreenshotCaptureResultCode::SUCCESS};
  };

  window().OnTextCaptured(region(u"Harbor 42"));
  ASSERT_TRUE(base::test::RunUntil([&] { return last_toast() == "Copied Text"; }));
  EXPECT_NE(clipboard().find("Harbor"), std::string::npos) << clipboard();

  window().OnTextCaptured(region(u""));
  ASSERT_TRUE(base::test::RunUntil([&] { return last_toast() == "No text found"; }));
  EXPECT_NE(clipboard().find("Harbor"), std::string::npos) << clipboard();
}
#endif

// Help: every item is enabled, and Copy Rio Info puts the versions a bug report needs on the clipboard.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandHelpItemsAreEnabled) {
  Open("/command/harbor.html");
  for (std::string_view id : {"platform.showLogs", "platform.copyDiagnostics",
                              "platform.shortcuts"}) {
    EXPECT_TRUE(window().CanRunCommand(id)) << id;
  }
  ASSERT_TRUE(window().RunCommand("platform.copyDiagnostics"));
  const std::string info =
      base::UTF16ToUTF8(ui::clipboard_test_util::ReadText(
          ui::Clipboard::GetForCurrentThread(), ui::ClipboardBuffer::kCopyPaste,
          nullptr));
  EXPECT_TRUE(base::StartsWith(info, "Rio ")) << info;
  EXPECT_NE(info.find("\nmacOS "), std::string::npos) << info;
}

// Help ▸ Contact the Team opens the issue form as the space's top Today tab, shown, like the command bar's help row.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandContactTheTeamOpensTheIssueForm) {
  Open("/command/harbor.html");
  const std::string before = window().ActiveRowId();
  ASSERT_TRUE(window().CanRunCommand("platform.contactTeam"));
  ASSERT_TRUE(window().RunCommand("platform.contactTeam"));
  const std::string row = window().ActiveRowId();
  ASSERT_NE(row, before);
  const CluTabTree& tree = window().model()->tree();
  ASSERT_TRUE(tree.Get(row));
  EXPECT_EQ(GURL(tree.Get(row)->url), GURL(kCluContactUrl));
  // The public issue form, the same one CONTACT_URL in rank.ts opens.
  EXPECT_EQ(GURL(tree.Get(row)->url),
            GURL("https://github.com/Levivajan/rio-releases/issues/new"));
  EXPECT_EQ(tree.Today(window().space_id()).front(), row);
}

// View ▸ Collapse Pinned Tabs folds the window's space's pinned section and reads Expand Pinned Tabs until it runs
// again; the main menu's Hide Sidebar and Pin Tab follow the window too.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandViewMenuTitlesFollowTheWindow) {
  Open("/command/harbor.html");
  const auto collapsed = [&] {
    return window()
        .model()
        ->tree()
        .SpaceState(window().space_id())
        .pinned_collapsed;
  };
  ASSERT_FALSE(collapsed());
  EXPECT_EQ(window().CommandTitle("tabs.collapsePinned"),
            u"Collapse Pinned Tabs");
  ASSERT_TRUE(window().RunCommand("tabs.collapsePinned"));
  EXPECT_TRUE(collapsed());
  EXPECT_EQ(window().CommandTitle("tabs.collapsePinned"),
            u"Expand Pinned Tabs");
  ASSERT_TRUE(window().RunCommand("tabs.collapsePinned"));
  EXPECT_FALSE(collapsed());

  EXPECT_EQ(window().CommandTitle("sidebar.toggle"), u"Hide Sidebar");
  ASSERT_TRUE(window().RunCommand("sidebar.toggle"));
  EXPECT_EQ(window().CommandTitle("sidebar.toggle"), u"Show Sidebar");
  ASSERT_TRUE(window().RunCommand("sidebar.toggle"));

  EXPECT_EQ(window().CommandTitle("tabs.togglePin"), u"Pin Tab");
  ASSERT_TRUE(window().RunCommand("tabs.togglePin"));
  EXPECT_EQ(window().CommandTitle("tabs.togglePin"), u"Unpin Tab");
}

// View ▸ Appearance ▸ Automatic, Light and Dark set the app's appearance.mode, and the menu checks the one in
// use.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandAppearanceItemsSetTheMode) {
  Open("/command/harbor.html");
  CluAppSettings* settings = CluAppSettings::Get();
  ASSERT_TRUE(settings);
  const std::pair<const char*, const char*> modes[] = {
      {"theme.appearanceDark", "dark"},
      {"theme.appearanceLight", "light"},
      {"theme.appearanceAuto", "auto"},
  };
  for (const auto& [id, mode] : modes) {
    EXPECT_TRUE(window().CanRunCommand(id)) << id;
    ASSERT_TRUE(window().RunCommand(id)) << id;
    EXPECT_EQ(settings->AppearanceMode(), mode) << id;
    for (const auto& [other, other_mode] : modes) {
      EXPECT_EQ(window().CommandChecked(other), other == id) << other;
    }
  }
  // The window's state follows (app.appearance.mode).
  ASSERT_TRUE(window().RunCommand("theme.appearanceDark"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const base::DictValue state = window().CurrentState();
    const std::string* mode =
        state.FindStringByDottedPath("app.appearance.mode");
    return mode && *mode == "dark";
  }));
  ASSERT_TRUE(window().RunCommand("theme.appearanceAuto"));
  EXPECT_EQ(window().CommandChecked("tabs.reveal"), std::nullopt);
}

// Tabs ▸ New Folder… adds a pinned folder to the window's space and asks the sidebar to rename it; Reveal Tab in
// Sidebar shows the sidebar, opens the row's Pinned section and folders and asks the sidebar to scroll to it; Reset all
// tabs in this Space sends every pinned row that left its pinned URL back, and is disabled with nothing to reset.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandTabsMenuFolderRevealAndReset) {
  const std::string row = Open("/command/harbor.html");
  CluModelService* m = window().model();
  const std::string space = window().space_id();
  const auto request_of = [&](const std::string& node_id) {
    const base::DictValue state = window().CurrentState();
    const std::string* id = state.FindStringByDottedPath(
        base::StrCat({"windows.", window().window_id(),
                      ".sidebarRequest.nodeId"}));
    return id && *id == node_id;
  };

  // New Folder….
  const std::vector<std::string> before = m->tree().Pinned(space);
  EXPECT_TRUE(window().CanRunCommand("tabs.newFolder"));
  ASSERT_TRUE(window().RunCommand("tabs.newFolder"));
  const std::vector<std::string> after = m->tree().Pinned(space);
  ASSERT_EQ(after.size(), before.size() + 1);
  std::string folder;
  for (const std::string& id : after) {
    if (!std::ranges::contains(before, id)) {
      folder = id;
    }
  }
  ASSERT_TRUE(m->tree().Get(folder) && m->tree().Get(folder)->is_folder());
  const std::optional<SidebarRequestSnapshot>& asked =
      window().sidebar_request_for_testing();
  ASSERT_TRUE(asked);
  EXPECT_EQ(asked->kind, "rename");
  EXPECT_EQ(asked->node_id, folder);
  const int rename_seq = asked->seq;
  EXPECT_TRUE(base::test::RunUntil([&] { return request_of(folder); }));

  // Reveal Tab in Sidebar: the row pinned inside the folder, the folder and the Pinned section closed, the sidebar
  // hidden.
  ASSERT_TRUE(window().TogglePin(row).has_value());
  ASSERT_TRUE(m->mutable_tree()
                  .Move(row, DropTarget::Pinned(space, folder, 0), m->NowMs())
                  .has_value());
  m->mutable_tree().Mutable(folder)->collapsed = true;
  m->mutable_tree().MutableSpaceState(space).pinned_collapsed = true;
  m->Commit();
  if (!shell().chrome_state().sidebar_hidden) {
    ASSERT_TRUE(window().RunCommand("sidebar.toggle"));
  }
  ASSERT_TRUE(shell().chrome_state().sidebar_hidden);
  ASSERT_EQ(window().ActiveRowId(), row);
  ASSERT_TRUE(window().RunCommand("tabs.reveal"));
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(m->tree().Get(folder)->collapsed);
  EXPECT_FALSE(m->tree().SpaceState(space).pinned_collapsed);
  ASSERT_TRUE(window().sidebar_request_for_testing());
  EXPECT_EQ(window().sidebar_request_for_testing()->kind, "reveal");
  EXPECT_EQ(window().sidebar_request_for_testing()->node_id, row);
  EXPECT_GT(window().sidebar_request_for_testing()->seq, rename_seq);
  EXPECT_TRUE(base::test::RunUntil([&] { return request_of(row); }));

  // Reset all tabs in this Space: nothing to reset until the pinned row leaves its pinned URL.
  EXPECT_FALSE(window().CanRunCommand("tabs.resetAll"));
  const GURL pinned_url = Contents(row)->GetLastCommittedURL();
  ASSERT_TRUE(content::NavigateToURL(Contents(row), Url("/command/inbox.html")));
  EXPECT_TRUE(window().CanRunCommand("tabs.resetAll"));
  ASSERT_TRUE(window().RunCommand("tabs.resetAll"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return Contents(row) && Contents(row)->GetLastCommittedURL() == pinned_url;
  }));
  EXPECT_FALSE(window().CanRunCommand("tabs.resetAll"));
}

// Tabs ▸ New Folder… and Reveal Tab in Sidebar close the Library, since only the tab list acts on their ask, and New
// Folder… moves the keyboard from the page to the frame so the rename field takes the typing. The tab list mounts from
// the patch that closes the Library and still acts on that ask: the new folder's row enters rename.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandTabsMenuClosesTheLibraryAndFocusesTheFrame) {
  Open("/command/harbor.html");
  window().SetArchivePanel(true);
  ASSERT_TRUE(window().archive_panel());
  ASSERT_TRUE(window().RunCommand("tabs.reveal"));
  EXPECT_FALSE(window().archive_panel());
  ASSERT_EQ(window().sidebar_request_for_testing()->kind, "reveal");

  window().SetArchivePanel(true);
  window().FocusActivePage();
  ASSERT_TRUE(window().RunCommand("tabs.newFolder"));
  EXPECT_FALSE(window().archive_panel());
  ASSERT_EQ(window().sidebar_request_for_testing()->kind, "rename");
  views::WebView* frame = shell().frame_view();
  ASSERT_TRUE(frame);
  EXPECT_TRUE(base::test::RunUntil([&] { return frame->HasFocus(); }));
  const std::string folder = window().sidebar_request_for_testing()->node_id;
  content::WebContents* frame_contents = shell().frame_contents();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(
               frame_contents,
               content::JsReplace(
                   "document.querySelector(`[data-folder-id=\"${CSS.escape($1)}\"]`)"
                   "?.dataset.renaming === 'true'",
                   folder))
        .ExtractBool();
  }));
}

// Focus mode hides the docked sidebar without hiding it as Cmd+S does, and only the docked tab list answers the
// Tabs menu's ask. New Folder… and Reveal Tab in Sidebar end focus mode first: the docked list shows, the new
// folder's row takes the keyboard in rename, and the revealed row is in the list on screen.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandTabsMenuInFocusModeShowsTheDockedList) {
  const std::string row = Open("/command/harbor.html");
  content::WebContents* frame_contents = shell().frame_contents();
  const auto docked_shown = [&] {
    return content::EvalJs(frame_contents, R"((() => {
      const frame = document.querySelector('[data-shell="sidebar-frame"]');
      const nav = [...(frame?.querySelectorAll('nav.clu-sidebar') ?? [])]
          .find((el) => !el.closest('.clu-sidebar-warm'));
      if (!nav || frame.dataset.hidden !== 'false' || nav.closest('[inert]')) {
        return false;
      }
      for (let node = nav; node; node = node.parentElement) {
        if (getComputedStyle(node).visibility === 'hidden') return false;
      }
      return true;
    })())")
        .ExtractBool();
  };
  ASSERT_TRUE(window().RunCommand("shell.focusMode"));
  ASSERT_TRUE(shell().chrome_state().focus_mode);
  ASSERT_FALSE(shell().chrome_state().sidebar_hidden);
  ASSERT_TRUE(shell().layout().hot_zone);
  EXPECT_TRUE(base::test::RunUntil([&] { return !docked_shown(); }));

  window().FocusActivePage();
  ASSERT_TRUE(window().RunCommand("tabs.newFolder"));
  EXPECT_FALSE(shell().chrome_state().focus_mode);
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(shell().layout().hot_zone);
  ASSERT_EQ(window().sidebar_request_for_testing()->kind, "rename");
  const std::string folder = window().sidebar_request_for_testing()->node_id;
  EXPECT_TRUE(base::test::RunUntil([&] { return docked_shown(); }));
  EXPECT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(
               frame_contents,
               content::JsReplace(
                   "(() => { const row = [...document.querySelectorAll("
                   "`[data-shell=\"sidebar-frame\"] "
                   "[data-folder-id=\"${CSS.escape($1)}\"]`)]"
                   ".find((el) => !el.closest('.clu-sidebar-warm'));"
                   " return row?.dataset.renaming === 'true' &&"
                   " row.contains(document.activeElement) &&"
                   " document.activeElement.tagName === 'INPUT'; })()",
                   folder))
        .ExtractBool();
  }));

  // Reveal Tab in Sidebar in focus mode, with the Cmd+S hide on top of it: both end and the docked list shows.
  ASSERT_TRUE(content::ExecJs(frame_contents,
                              "document.activeElement?.blur()"));
  ASSERT_TRUE(window().RunCommand("shell.focusMode"));
  ASSERT_TRUE(window().RunCommand("sidebar.toggle"));
  ASSERT_TRUE(shell().chrome_state().focus_mode);
  ASSERT_TRUE(shell().chrome_state().sidebar_hidden);
  ASSERT_EQ(window().ActiveRowId(), row);
  ASSERT_TRUE(window().RunCommand("tabs.reveal"));
  EXPECT_FALSE(shell().chrome_state().focus_mode);
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(shell().layout().hot_zone);
  ASSERT_EQ(window().sidebar_request_for_testing()->kind, "reveal");
  EXPECT_EQ(window().sidebar_request_for_testing()->node_id, row);
  EXPECT_TRUE(base::test::RunUntil([&] { return docked_shown(); }));
}

// Spaces ▸ New Space… with the sidebar hidden shows the sidebar first: Create a Space draws over its empty stretch.
// Edit Space… leaves a hidden sidebar hidden. An open Library closes for New Space, docked or floating, so the panel
// never draws over it.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandNewSpaceShowsTheHiddenSidebar) {
  Open("/command/harbor.html");
  if (!shell().chrome_state().sidebar_hidden) {
    ASSERT_TRUE(window().RunCommand("sidebar.toggle"));
  }
  ASSERT_TRUE(shell().chrome_state().sidebar_hidden);
  ASSERT_TRUE(window().RunCommand("spaces.editSpace"));
  EXPECT_TRUE(shell().chrome_state().sidebar_hidden);
  window().SetArchivePanel(true);
  ASSERT_TRUE(window().RunCommand("spaces.newSpace"));
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(window().archive_panel());

  // Docked: Library ▸ Spaces ▸ New Space closes the Library too.
  window().SetArchivePanel(true);
  ASSERT_TRUE(window().archive_panel());
  ASSERT_TRUE(window().RunCommand("spaces.newSpace"));
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(window().archive_panel());

  // Focus mode hides the docked sidebar too, without setting sidebar_hidden: New Space ends it, so the panel draws
  // over the docked sidebar and not over the page.
  ASSERT_TRUE(window().RunCommand("shell.focusMode"));
  ASSERT_TRUE(shell().chrome_state().focus_mode);
  ASSERT_FALSE(shell().chrome_state().sidebar_hidden);
  ASSERT_TRUE(shell().layout().hot_zone);
  EXPECT_FALSE(window().DockedSidebarShown());
  ASSERT_TRUE(window().RunCommand("spaces.newSpace"));
  EXPECT_FALSE(shell().chrome_state().focus_mode);
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
  EXPECT_FALSE(shell().layout().hot_zone);
  EXPECT_TRUE(window().DockedSidebarShown());
  EXPECT_GE(shell().layout().page_rect.x(),
            shell().layout().sidebar_rect.right());
}

// A private window has no spaces, so the Spaces menu lists none of its rows (an empty title hides a row), and none
// of them runs; the main window still names its first space.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandPrivateWindowHidesSpaceRows) {
  BrowserWindowInterface* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  CluWindowState* private_window = CluWindowState::GetOrCreate(incognito);
  ASSERT_TRUE(private_window);
  for (int n = 1; n <= 9; ++n) {
    const std::string id = "spaces.goto" + base::NumberToString(n);
    EXPECT_EQ(private_window->CommandTitle(id), std::u16string()) << id;
    EXPECT_FALSE(private_window->CanRunCommand(id)) << id;
  }
  EXPECT_NE(window().CommandTitle("spaces.goto1"), std::u16string());
  // New Space…, Edit Theme…, Edit Space…, Next and Previous Space make no sense in the private window's one
  // unsaved space, so they hide there (an empty title) instead of showing greyed; the main window keeps them.
  for (const char* id : {"spaces.newSpace", "theme.editTheme",
                         "spaces.editSpace", "spaces.next", "spaces.prev"}) {
    EXPECT_EQ(private_window->CommandTitle(id), std::u16string()) << id;
    EXPECT_FALSE(private_window->CanRunCommand(id)) << id;
    EXPECT_FALSE(window().CommandTitle(id).has_value()) << id;
  }
  EXPECT_TRUE(window().CanRunCommand("spaces.newSpace"));
  EXPECT_TRUE(window().CanRunCommand("theme.editTheme"));
  // A private window keeps no rows, so New Folder… and Reset all tabs in this Space are disabled there.
  EXPECT_FALSE(private_window->CanRunCommand("tabs.newFolder"));
  EXPECT_FALSE(private_window->CanRunCommand("tabs.resetAll"));
}

// View ▸ Clear Cookies and Refresh drops the shown site's cookies and reloads it; Clear Cache and Refresh reloads
// it too. Neither runs for a page that isn't on the web.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandClearSiteDataAndRefresh) {
  const std::string row = Open("/command/harbor.html");
  content::WebContents* page = Contents(row);
  ASSERT_TRUE(
      content::ExecJs(page, "document.cookie = 'kept=1; max-age=600'"));
  ASSERT_EQ(content::EvalJs(page, "document.cookie"), "kept=1");
  {
    content::TestNavigationObserver reloaded(page);
    ASSERT_TRUE(window().RunCommand("page.clearCookiesAndRefresh"));
    reloaded.Wait();
  }
  EXPECT_EQ(content::EvalJs(page, "document.cookie"), "");
  {
    content::TestNavigationObserver reloaded(page);
    ASSERT_TRUE(window().RunCommand("page.clearCacheAndRefresh"));
    reloaded.Wait();
  }
  ASSERT_TRUE(content::NavigateToURL(page, GURL("about:blank")));
  EXPECT_FALSE(window().CanRunCommand("page.clearCookiesAndRefresh"));
  EXPECT_FALSE(window().CanRunCommand("page.clearCacheAndRefresh"));
}

// Window ▸ Stay On Top floats the window above normal ones, and running it again puts it back.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest, CluCommandStayOnTopToggles) {
  Open("/command/harbor.html");
  ui::BaseWindow* shown = browser()->GetWindow();
  ASSERT_EQ(shown->GetZOrderLevel(), ui::ZOrderLevel::kNormal);
  ASSERT_TRUE(window().RunCommand("window.stayOnTop"));
  EXPECT_EQ(shown->GetZOrderLevel(), ui::ZOrderLevel::kFloatingWindow);
  ASSERT_TRUE(window().RunCommand("window.stayOnTop"));
  EXPECT_EQ(shown->GetZOrderLevel(), ui::ZOrderLevel::kNormal);
}

// Cmd+L then Enter with no edit reloads once (04-command F2); an edit navigates the same tab.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandEditUrlReloadsOrNavigatesTheSameTab) {
  const std::string row = Open("/command/loads.html");
  content::WebContents* page = Contents(row);
  EXPECT_EQ(content::EvalJs(page, "document.getElementById('loads').textContent"),
            "1");
  ASSERT_TRUE(window().RunCommand("commandbar.editUrl"));
  EXPECT_EQ(true, content::EvalJs(overlay(),
                                  content::JsReplace(kWaitForSelector,
                                                     "[data-kind=\"reload\"]")));
  content::SimulateKeyPress(overlay(), ui::DomKey::ENTER, ui::DomCode::ENTER,
                            ui::VKEY_RETURN, false, false, false, false);
  // The reload replaces the page's frame, so a poll can land on the frame that is going away.
  EXPECT_TRUE(base::test::RunUntil([&] {
    const content::EvalJsResult loads =
        content::EvalJs(page, "document.getElementById('loads').textContent");
    return loads.is_ok() && loads.ExtractString() == "2";
  }));
  EXPECT_EQ(window().FindTab(row)->GetContents(), page);

  const int pages = strip().count();
  ASSERT_TRUE(window().RunCommand("commandbar.editUrl"));
  ASSERT_TRUE(window().command_session());
  const std::string session = window().command_session()->session_id;
  content::TestNavigationObserver moved(page);
  ASSERT_TRUE(IsOk(Execute(
      session, mojom::CommandResult::NewNavigate(mojom::CommandResultNavigate::New(
                   "navigate", Url("/command/harbor.html").spec())))));
  moved.Wait();
  EXPECT_EQ(page->GetLastCommittedURL(), Url("/command/harbor.html"));
  EXPECT_EQ(strip().count(), pages);
}

// A page typed into the bar goes to the top of Today even with a Today row active below the top (an Instant
// Links tab landed under the active row, off the visible list). Chromium names the active page as the new tab's opener.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandANewTabFromTheBarGoesToTheTopOfToday) {
  const std::string a = Open("/command/harbor.html");
  const std::string b = Open("/command/inbox.html");
  Open("/command/loads.html");
  ASSERT_TRUE(window().ActivateRow(b));
  const CluTabTree& tree = window().model()->tree();
  const std::vector<std::string>& today = tree.Today(window().space_id());
  ASSERT_NE(std::ranges::find(today, b), today.begin());
  const std::string session = OpenBar();
  content::TestNavigationObserver opened(Url("/command/harbor.html?typed"));
  opened.StartWatchingNewWebContents();
  ASSERT_TRUE(IsOk(Execute(
      session, mojom::CommandResult::NewNavigate(mojom::CommandResultNavigate::New(
                   "navigate", Url("/command/harbor.html?typed").spec())))));
  opened.Wait();
  const std::string row = window().ActiveRowId();
  ASSERT_NE(row, a);
  ASSERT_NE(row, b);
  EXPECT_EQ(tree.Today(window().space_id()).front(), row);
}


// Command bar context rows: an empty Cmd+T lists favorites (they have no
// space) as title-only Switch to Tab rows and ends with Contact the Team; a query naming a folder offers Open and Pin
// to, which the overlay runs; an unchanged Cmd+L lists the current tab, then Contact the Team.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest,
                       CluCommandFavoritesContextRowsAndEditUrl) {
  const std::string harbor = Open("/command/harbor.html");
  ASSERT_TRUE(
      window().MoveRows({harbor}, DropTarget::Favorite(0), false).has_value());
  const std::string inbox = Open("/command/inbox.html");

  OpenBar();
  std::vector<std::string> rows =
      RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
        return Contains(rows, "tab|recent|Harbor Lighthouse Notes") &&
               !rows.empty() &&
               rows.back() == "navigate|help|Contact the Team";
      }));
  EXPECT_LE(rows.size(), 5u);
  EXPECT_EQ(0, content::EvalJs(overlay(), R"(
    document.querySelectorAll('[data-surface="command"] [data-kind="tab"] .clu-cmd-url').length)"));
  window().CloseCommandBar();

  CluModelService* model = window().model();
  ASSERT_TRUE(model);
  const std::string folder = model->mutable_tree()
                                 .CreateFolder(window().space_id(), std::string(),
                                               "Quay folder", std::nullopt,
                                               model->NowMs())
                                 .value();
  model->mutable_tree().Mutable(folder)->collapsed = true;
  model->Commit();
  ASSERT_TRUE(window()
                  .MoveRows({inbox}, DropTarget::Pinned(window().space_id(), "", 0),
                            false)
                  .has_value());
  window().Flush();

  OpenBar();
  Type("quay");
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return Contains(rows, "action|actions|Open") &&
           Contains(rows, "action|actions|Pin to");
  }));
  ASSERT_TRUE(content::ExecJs(overlay(), R"(
    Array.from(document.querySelectorAll('[data-surface="command"] [role=option]'))
        .find(r => r.querySelector('.clu-cmd-title')?.textContent === 'Pin to')
        .click())"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const TabNode* node = model->tree().Get(inbox);
    return node && node->parent_id == folder;
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().command_session(); }));

  OpenBar();
  Type("quay");
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return Contains(rows, "action|actions|Open");
  }));
  ASSERT_TRUE(content::ExecJs(overlay(), R"(
    Array.from(document.querySelectorAll('[data-surface="command"] [role=option]'))
        .find(r => r.querySelector('.clu-cmd-title')?.textContent === 'Open')
        .click())"));
  EXPECT_TRUE(base::test::RunUntil([&] {
    const TabNode* node = model->tree().Get(folder);
    return node && !node->collapsed;
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return !window().command_session(); }));
  EXPECT_EQ(true, content::EvalJs(overlay(), kWaitForNoBar));

  ASSERT_TRUE(window().RunCommand("commandbar.editUrl"));
  rows = RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return !rows.empty() && rows.back() == "navigate|help|Contact the Team";
  }));
  EXPECT_EQ(rows.front(), "reload|top|Inbox (3) - Gmail");
  window().CloseCommandBar();
}

// One typed word gets the Ask row, with the saved
// assistant (ChatGPT until the user picks another).
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest, CluCommandAskRowForOneWord) {
  UseFixtureEngine();
  Open("/command/harbor.html");
  OpenBar();
  Type("hello");
  RowsWhen(base::BindRepeating([](const std::vector<std::string>& rows) {
    return Contains(rows, "navigate|ask|hello");
  }));
  EXPECT_EQ("Ask ChatGPT", content::EvalJs(overlay(), R"(
    document.querySelector('[data-surface="command"] [data-group="ask"] [data-ask-picker]')
        ?.textContent ?? '')"));
  window().CloseCommandBar();
}

// The panel lets about 11% of the page through, so its color is at 89% and still frosted,
// in light and in dark.
IN_PROC_BROWSER_TEST_F(CluCommandBrowserTest, CluCommandPanelIsFrostedAt89Percent) {
  Open("/command/harbor.html");
  OpenBar();
  for (const char* theme : {"light", "dark"}) {
    SCOPED_TRACE(theme);
    EXPECT_EQ("0.89|blur(30px) saturate(1.4)", content::EvalJs(overlay(), content::JsReplace(R"(
      (() => {
        const root = document.documentElement;
        const before = root.dataset.theme;
        root.dataset.theme = $1;
        const style = getComputedStyle(document.querySelector('[data-surface="command"] .clu-cmd'));
        const alpha = style.backgroundColor.match(/\/\s*([\d.]+)\)/)?.[1] ?? '1';
        const filter = style.backdropFilter;
        if (before === undefined) delete root.dataset.theme; else root.dataset.theme = before;
        return alpha + '|' + filter;
      })())", theme)));
  }
  window().CloseCommandBar();
}

}  // namespace
}  // namespace clu
