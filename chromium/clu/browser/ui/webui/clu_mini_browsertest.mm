// Copyright 2026 The Rio Authors. All rights reserved.

#import <Cocoa/Cocoa.h>

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/test/run_until.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#import "chrome/browser/app_controller_mac.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/commands/command_table.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/multitask/clu_mini_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/views/frame/clu_shell_hooks.h"
#include "clu/browser/ui/webui/clu_command_bar.h"
#include "clu/browser/ui/webui/clu_mini_windows.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_handlers.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/base/apple/url_conversions.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/base/base_window.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "url/gurl.h"
#include "url/url_constants.h"

// The CluMini* checks of the M6 "Done when" row (docs/chromium/PLAN.md section
// 6): the second box of docs/PLAN.md 12.10, a link from another app through
// AppController's open-url, the cap, the move into a space without a reload,
// the confirm strip for another profile, and the archive on close and after 6
// idle hours.
namespace clu {
namespace {

using CallFuture = base::test::TestFuture<mojom::CallResultPtr>;

constexpr char kWaitForSelector[] = R"(
  new Promise(resolve => {
    const check = () => document.querySelector($1) ? resolve(true)
                                                     : setTimeout(check, 20);
    check();
  }))";

bool IsOk(const mojom::CallResultPtr& result) {
  return result && result->is_value_json();
}

// What app_controller_mac_browsertest.mm sends for a link opened in another
// app.
void SendOpenUrlToAppController(const GURL& url) {
  [NSApp.delegate application:NSApp openURLs:@[ net::NSURLWithGURL(url) ]];
}

class CluMiniBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    // --window-size sizes every new window (GetSavedWindowBoundsAndShowState),
    // the Mini windows' 880 by 620 too. The main window has it already;
    // --window-position stays, so Mini windows open on the tests display.
    base::CommandLine::ForCurrentProcess()->RemoveSwitch(switches::kWindowSize);
    embedded_test_server()->ServeFilesFromSourceDirectory("clu/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(base::test::RunUntil([&] { return window().model(); }));
    ASSERT_TRUE(content::WaitForLoadStop(shell().frame_contents()));
    // Mini windows never take focus from the test (05 "In test mode it uses
    // showInactive()").
    mini_windows::SetShowInactiveForTesting(true);
    std::ignore = AppController.sharedController;
  }

  void TearDownOnMainThread() override {
    mini_windows::SetShowInactiveForTesting(false);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  CluShell& shell() { return *CluShell::From(browser()); }
  CluWindowState& window() { return *CluWindowState::GetOrCreate(browser()); }
  CluModelService& model() { return *window().model(); }
  std::string space() { return window().space_id(); }

  GURL Url(const std::string& page) {
    return embedded_test_server()->GetURL(base::StrCat({"/", page}));
  }

  // The Mini window that shows `url` once it loaded.
  CluWindowState* WaitForMini(size_t count) {
    EXPECT_TRUE(base::test::RunUntil(
        [&] { return mini_windows::All().size() == count; }));
    std::vector<CluWindowState*> minis = mini_windows::All();
    if (minis.size() != count) {
      return nullptr;
    }
    CluWindowState* mini = minis.back();
    tabs::TabInterface* tab = mini->ActiveTab();
    EXPECT_TRUE(tab);
    if (tab) {
      EXPECT_TRUE(content::WaitForLoadStop(tab->GetContents()));
    }
    return mini;
  }

  const base::DictValue& MiniState(CluWindowState& mini) {
    state_ = mini.CurrentState();
    return *state_.FindDict("windows")->FindDict(mini.window_id());
  }

  void WaitForSelector(content::WebContents* contents,
                       const std::string& selector) {
    EXPECT_EQ(true, content::EvalJs(contents, content::JsReplace(
                                                  kWaitForSelector, selector)));
  }

  ArchivePage Archive() {
    base::RunLoop loop;
    model().FlushForTesting(loop.QuitClosure());
    loop.Run();
    base::test::TestFuture<ArchivePage> page;
    model().ListArchive(std::string(), std::string(), 100, page.GetCallback());
    return page.Take();
  }

  base::DictValue state_;
  base::SimpleTestTickClock clock_;
};

// 12.10 box 2 ---------------------------------------------------------------------------------------------------------

IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniALinkFromAnotherAppOpensAMiniWindow) {
  const GURL url = Url("multitask/target.html");
  SendOpenUrlToAppController(url);
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  // A popup with the Clu layout in Mini mode, 880 by 620, holding the one page;
  // no row in any space.
  EXPECT_EQ(mini->browser()->GetType(), BrowserWindowInterface::TYPE_POPUP);
  EXPECT_TRUE(mini->shell().is_mini());
  EXPECT_EQ(mini->browser()->GetWindow()->GetBounds().size(), kMiniSize);
  // macOS centers the window buttons on the 36 pt toolbar row (clu::GetTitlebarHeight).
  NSWindow* ns_window =
      mini->browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  NSButton* close = [ns_window standardWindowButton:NSWindowCloseButton];
  ASSERT_TRUE(base::test::RunUntil([&] {
    const NSRect in_window = [close convertRect:close.bounds toView:nil];
    return std::abs(NSHeight(ns_window.frame) - NSMidY(in_window) -
                    kMiniToolbarHeight / 2.0) <= 1;
  }));
  EXPECT_EQ(mini->ActiveTab()->GetContents()->GetLastCommittedURL(), url);
  const base::DictValue& state = MiniState(*mini);
  EXPECT_EQ(*state.FindString("kind"), "mini");
  EXPECT_EQ(*state.FindStringByDottedPath("mini.targetSpaceId"), space());
  EXPECT_FALSE(model().tree().Get(*state.FindStringByDottedPath("mini.tabId")));
  // The toolbar: Back, the host, the action row, Site controls, "Open in
  // <space>" and Close.
  content::WebContents* frame = mini->shell().frame_contents();
  ASSERT_TRUE(content::WaitForLoadStop(frame));
  WaitForSelector(frame, "[data-mini-toolbar] [data-mini-open-in]");
  WaitForSelector(frame, "[data-slot=\"action-row\"]");
  WaitForSelector(frame, "[data-slot=\"site-controls\"]");
  // A file: URL or another scheme stays with Chromium.
  SendOpenUrlToAppController(GURL("file:///etc/hosts"));
  EXPECT_EQ(mini_windows::All().size(), 1u);
}

// The Mini window opens and closes with AppKit's document-window motion (grow
// from 0.98 with a 95 ms fade in, 150 ms fade out, docs/chromium/motion.md);
// main windows keep Chromium's none.
IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest, CluMiniOpensAndClosesWithTheWindowAnimation) {
  SendOpenUrlToAppController(Url("multitask/target.html"));
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  NSWindow* ns_window =
      mini->browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  EXPECT_TRUE(base::test::RunUntil([&] {
    return ns_window.animationBehavior == NSWindowAnimationBehaviorDocumentWindow;
  }));
  NSWindow* main_window = browser()->GetWindow()->GetNativeWindow().GetNativeNSWindow();
  EXPECT_NE(main_window.animationBehavior, NSWindowAnimationBehaviorDocumentWindow);
}

// A Mini window has no sidebar: one opened after the main window hid its sidebar keeps its window buttons and its
// page rect, and the sidebar toggle and focus mode from it act on the main window.
IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniKeepsItsLayoutWhenTheSidebarIsHidden) {
  const int toggle = FindCommand("sidebar.toggle")->idc;
  ASSERT_TRUE(HandleBrowserCommand(browser(), toggle));
  ASSERT_TRUE(shell().chrome_state().sidebar_hidden);
  SendOpenUrlToAppController(Url("multitask/target.html"));
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  CluShell& mini_shell = mini->shell();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !mini_shell.layout().content_size.IsEmpty(); }));
  const gfx::Rect page =
      MiniPageRect(mini_shell.layout().content_size, /*confirming=*/false);
  EXPECT_TRUE(mini_shell.layout().traffic_lights);
  EXPECT_FALSE(mini_shell.layout().hot_zone);
  EXPECT_EQ(mini_shell.layout().page_rect, page);

  // Cmd+S and Cmd+Shift+F in the Mini window: its layout stays, the main
  // window's sidebar comes back.
  ASSERT_TRUE(base::test::RunUntil([&] { return mini_shell.client(); }));
  EXPECT_TRUE(HandleBrowserCommand(mini->browser(), toggle));
  EXPECT_TRUE(HandleBrowserCommand(mini->browser(),
                                   FindCommand("shell.focusMode")->idc));
  EXPECT_EQ(mini_shell.layout().page_rect, page);
  EXPECT_TRUE(mini_shell.layout().traffic_lights);
  EXPECT_FALSE(mini_shell.layout().hot_zone);
  EXPECT_FALSE(shell().chrome_state().sidebar_hidden);
}

IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniTheSeventhLinkOpensAsATodayTab) {
  for (size_t i = 0; i < kMaxMiniWindows; ++i) {
    SendOpenUrlToAppController(Url("multitask/target.html"));
    ASSERT_TRUE(WaitForMini(i + 1));
  }
  const size_t rows = model().tree().Today(space()).size();
  SendOpenUrlToAppController(Url("multitask/links.html"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return model().tree().Today(space()).size() == rows + 1; }));
  EXPECT_EQ(mini_windows::All().size(), kMaxMiniWindows);
  const TabNode* top =
      model().tree().Get(model().tree().Today(space()).front());
  EXPECT_EQ(top->url, Url("multitask/links.html").spec());
  const base::DictValue main = window().CurrentState();
  const base::ListValue* toasts = main.FindDict("windows")
                                      ->FindDict(window().window_id())
                                      ->FindList("toasts");
  ASSERT_FALSE(toasts->empty());
  EXPECT_EQ(*toasts->back().GetDict().FindString("text"),
            MiniCapToast(kMaxMiniWindows));
}

// With incognito forced by policy, a link from another app is Chromium's: it
// opens in a private window, never a Mini window of the normal profile.
IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniForcedIncognitoLeavesTheLinkToChromium) {
  browser()->GetProfile()->GetPrefs()->SetInteger(
      policy::policy_prefs::kIncognitoModeAvailability,
      static_cast<int>(policy::IncognitoModeAvailability::kForced));
  SendOpenUrlToAppController(Url("multitask/target.html"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (BrowserWindowInterface* b : GetAllBrowserWindowInterfaces()) {
      if (b->GetProfile()->IsOffTheRecord()) {
        return true;
      }
    }
    return false;
  }));
  EXPECT_TRUE(mini_windows::All().empty());
}

// At the cap a link opens as a Today tab of its own profile only: a Work link
// never lands in the Personal window.
IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniAtTheCapALinkStaysInItsProfile) {
  CluProfilesHandler profiles(window().GetWeakPtr());
  CallFuture made;
  profiles.Create(mojom::ProfilesCreateInput::New("Work"), made.GetCallback());
  ASSERT_TRUE(IsOk(made.Get()));
  const std::string profile_id =
      *base::JSONReader::ReadDict(made.Get()->get_value_json(),
                                  base::JSON_PARSE_RFC)
           ->FindString("profileId");
  base::test::TestFuture<Profile*> loaded;
  CluSpacesService::Get()->LoadProfile(profile_id, loaded.GetCallback());
  Profile* work = loaded.Get();
  ASSERT_TRUE(work);
  ASSERT_NE(work, browser()->GetProfile());

  for (size_t i = 0; i < kMaxMiniWindows; ++i) {
    SendOpenUrlToAppController(Url("multitask/target.html"));
    ASSERT_TRUE(WaitForMini(i + 1));
  }
  const size_t rows = model().tree().Today(space()).size();
  // No main window of Work: nothing opens here, the caller hands the link to
  // Chromium.
  EXPECT_FALSE(mini_windows::Open(work, Url("multitask/links.html"),
                                  /*command_bar=*/false));
  EXPECT_EQ(model().tree().Today(space()).size(), rows);
}

// The empty prompt is a 490 by 329 window at the screen's
// top-right; its page loading grows it to 880 by 620 from that corner.
IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniThePromptOpensSmallAtTheTopRightAndGrows) {
  content::WebContents* page = mini_windows::Open(
      browser()->GetProfile(), GURL(url::kAboutBlankURL), /*command_bar=*/true);
  ASSERT_TRUE(page);
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  ui::BaseWindow* window = mini->browser()->GetWindow();
  const gfx::Rect prompt = window->GetBounds();
  EXPECT_EQ(prompt.size(), kMiniPromptSize);
  const gfx::Rect work =
      display::Screen::Get()->GetDisplayMatching(prompt).work_area();
  EXPECT_EQ(prompt.right(), work.right() - kMiniPromptInset);
  EXPECT_EQ(prompt.y(), work.y() + kMiniPromptInset);

  ASSERT_TRUE(content::NavigateToURL(page, Url("multitask/target.html")));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window->GetBounds().size() == kMiniSize; }));
  EXPECT_EQ(window->GetBounds().right(), prompt.right());
  EXPECT_EQ(window->GetBounds().y(), prompt.y());
}

IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniMovesIntoTheSpaceWithoutAReload) {
  SendOpenUrlToAppController(Url("command/loads.html"));
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  content::WebContents* page = mini->ActiveTab()->GetContents();
  EXPECT_EQ("1", content::EvalJs(page, "sessionStorage.getItem('loads')"));
  // Cmd+O (mini.openInSpace) moves the live page as the top Today tab of the
  // main window's space.
  EXPECT_TRUE(mini->RunCommand("mini.openInSpace"));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return mini_windows::All().empty(); }));
  const std::string row = window().ActiveRowId();
  EXPECT_EQ(model().tree().Today(space()).front(), row);
  tabs::TabInterface* tab = window().FindTab(row);
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->GetContents(), page);
  EXPECT_EQ("1", content::EvalJs(page, "sessionStorage.getItem('loads')"));
  // A move is no close: nothing went to the archive.
  for (const ArchiveRow& archived : Archive().rows) {
    EXPECT_NE(archived.reason, ArchiveReason::kMini);
  }
}

IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniASpaceOfAnotherProfileAsksFirst) {
  // A second profile with its own space.
  CluProfilesHandler profiles(window().GetWeakPtr());
  CallFuture made;
  profiles.Create(mojom::ProfilesCreateInput::New("Work"), made.GetCallback());
  ASSERT_TRUE(IsOk(made.Get()));
  const std::string profile_id =
      *base::JSONReader::ReadDict(made.Get()->get_value_json(),
                                  base::JSON_PARSE_RFC)
           ->FindString("profileId");
  base::expected<std::string, SpacesError> work =
      CluSpacesService::Get()->CreateSpace("Work", std::nullopt, std::nullopt,
                                           profile_id);
  ASSERT_TRUE(work.has_value());

  SendOpenUrlToAppController(Url("command/loads.html"));
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  CluMiniHandler handler(mini->GetWeakPtr());
  CallFuture asked;
  handler.MoveToSpace(
      mojom::MiniMoveToSpaceInput::New(work.value(), std::nullopt),
      asked.GetCallback());
  ASSERT_TRUE(IsOk(asked.Get()));
  // The confirm strip names the space; nothing moved yet.
  EXPECT_EQ(MiniState(*mini).FindBoolByDottedPath("mini.confirming"), true);
  EXPECT_EQ(MiniState(*mini).FindBoolByDottedPath("mini.profileMismatch"),
            true);
  WaitForSelector(mini->shell().frame_contents(), "[data-mini-confirm]");
  // Cancel closes the strip; Move opens the URL fresh in the other profile.
  CallFuture cancelled;
  handler.CancelMove(cancelled.GetCallback());
  ASSERT_TRUE(IsOk(cancelled.Get()));
  EXPECT_EQ(MiniState(*mini).FindBoolByDottedPath("mini.confirming"), false);
  CallFuture moved;
  handler.MoveToSpace(mojom::MiniMoveToSpaceInput::New(work.value(), true),
                      moved.GetCallback());
  ASSERT_TRUE(IsOk(moved.Get()));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return mini_windows::All().empty(); }));
  CluWindowState* other = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (CluWindowState* w : space_actions::AllWindows()) {
      if (w->space_id() == work.value() && w->model() &&
          !w->ActiveRowId().empty()) {
        other = w;
      }
    }
    return other != nullptr;
  }));
  content::WebContents* fresh = other->ActiveTab()->GetContents();
  ASSERT_TRUE(content::WaitForLoadStop(fresh));
  EXPECT_EQ(fresh->GetLastCommittedURL(), Url("command/loads.html"));
  EXPECT_NE(fresh->GetBrowserContext(), browser()->GetProfile());
}

IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniClosingOrSixIdleHoursArchiveThePage) {
  SendOpenUrlToAppController(Url("multitask/target.html?closed"));
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  mini->browser()->GetWindow()->Close();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return mini_windows::All().empty(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (const ArchiveRow& row : Archive().rows) {
      if (row.reason == ArchiveReason::kMini &&
          row.url == Url("multitask/target.html?closed").spec()) {
        return true;
      }
    }
    return false;
  }));

  // Six hours nobody used it: the window closes the same way.
  SendOpenUrlToAppController(Url("multitask/target.html?idle"));
  mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  // The fixture's clock outlives the window, which is only scheduled for
  // deletion when it closes.
  base::SimpleTestTickClock& clock = clock_;
  clock.SetNowTicks(base::TimeTicks::Now());
  mini->multitask().SetMiniTickClockForTesting(&clock);
  clock.Advance(kMiniIdle - base::Minutes(1));
  mini->multitask().CheckMiniIdleForTesting();
  EXPECT_EQ(mini_windows::All().size(), 1u);
  clock.Advance(base::Minutes(1));
  mini->multitask().CheckMiniIdleForTesting();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return mini_windows::All().empty(); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    for (const ArchiveRow& row : Archive().rows) {
      if (row.reason == ArchiveReason::kMini &&
          row.url == Url("multitask/target.html?idle").spec()) {
        return true;
      }
    }
    return false;
  }));
}

IN_PROC_BROWSER_TEST_F(CluMiniBrowserTest,
                       CluMiniCmdOptionNOpensTheBarInMiniMode) {
  EXPECT_TRUE(window().RunCommand("mini.new"));
  CluWindowState* mini = WaitForMini(1);
  ASSERT_TRUE(mini);
  ASSERT_TRUE(mini->command_session());
  EXPECT_EQ(mini->command_session()->mode, "mini");
  // A result loads in the Mini window's page.
  CluCommandHandler bar(mini->GetWeakPtr(), mini->profile());
  CallFuture ran;
  bar.Execute(
      mojom::CommandExecuteInput::New(
          mini->command_session()->session_id,
          mojom::CommandResult::NewNavigate(mojom::CommandResultNavigate::New(
              "navigate", Url("multitask/target.html").spec())),
          mojom::CommandExecuteInputDisposition::kDefault, "target"),
      ran.GetCallback());
  ASSERT_TRUE(IsOk(ran.Get()));
  content::WebContents* page = mini->ActiveTab()->GetContents();
  ASSERT_TRUE(content::WaitForLoadStop(page));
  EXPECT_EQ(page->GetLastCommittedURL(), Url("multitask/target.html"));
  EXPECT_EQ(mini_windows::All().size(), 1u);
}

}  // namespace
}  // namespace clu
