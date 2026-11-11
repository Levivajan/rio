// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_PEEK_H_
#define CLU_BROWSER_UI_WEBUI_CLU_PEEK_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/rect.h"

class BrowserWindowInterface;
class GURL;

namespace content {
struct Referrer;
class WebContents;
}  // namespace content

namespace clu {

class CluWindowState;

// One window's Peek (docs/plan/05-multitask.md "Peek", docs/chromium/m6.md): a
// page with no tab over the window's page area, in CluShell's CluPeekView. The
// under-peek overlay draws the scrim, the card animation, the host and the rail
// from windows[id].peek; the page attaches at the card once the overlay reports
// the open animation done, or after 250 ms. "Open as tab" hands the same
// WebContents to the window's strip as a Today row, so the page, its history
// and its state carry over without a reload. One Peek per window: a new one
// navigates the open Peek and replays the animation. Peeks are never stored.
class CluPeek : public content::WebContentsDelegate,
                public content::WebContentsObserver {
 public:
  enum class NavAction { kBack, kForward, kReload, kHardReload, kStop };

  explicit CluPeek(CluWindowState& window);
  CluPeek(const CluPeek&) = delete;
  CluPeek& operator=(const CluPeek&) = delete;
  ~CluPeek() override;

  bool is_open() const { return page_ != nullptr; }
  content::WebContents* page() { return page_.get(); }
  // The row the Peek opened over.
  const std::string& source_row_id() const { return source_row_id_; }

  // Hover previews: once the page has loaded, the on-device model writes two or three sentences about it
  // (windows[id].peek.summary), which the overlay shows beside the card. Only a Peek the hover opened asks.
  void Summarize();
  // The open Peek is a hover preview: another overlay opening closes it.
  bool from_hover() const { return from_hover_ && is_open(); }
  // The strip at the card's top that holds the summary; PEEK_SUMMARY_STRIP in PeekChrome.tsx.
  static constexpr int kSummaryStrip = 76;
  // Opens `url` over row `source_row_id`, or loads it in the open Peek.
  void Open(const std::string& source_row_id,
            const GURL& url,
            const content::Referrer& referrer);
  // A page the source made (target=_blank, window.open) becomes the Peek,
  // opener and all. Returns it.
  content::WebContents* Adopt(const std::string& source_row_id,
                              std::unique_ptr<content::WebContents> page);
  // Esc, Cmd+W, the scrim, the rail's Close, a tab or space switch: the page
  // goes, the overlay shrinks the card.
  void Close();
  // Cmd+Shift+O and the rail's Open as tab: the page becomes a Today row right
  // after a Today source, else at the top of the space's Today, and shows.
  // Returns the row id.
  std::optional<std::string> Expand();
  // The rail's Open in split: the page becomes a Today row and a pane beside
  // the source. False (the Peek stays) when the source cannot take a second
  // pane.
  bool ToSplit();
  bool Nav(NavAction action);
  // Copy URL on the Peek page (a page command while the Peek has focus).
  void CopyUrl(bool markdown);
  // A page command (nav.*, edit.copyUrl*, find.*, zoom.*) while the Peek
  // takes page commands: from the key table, the command bar, and Chromium's
  // menu items picked with the mouse (CluShell::Client::PeekPageCommand).
  // Nullopt when the Peek does not take it, so the window goes on; else whether
  // it would run (`run` false: Back without history, Stop while idle, Zoom at
  // its limit do not) or ran.
  std::optional<bool> RunPageCommand(std::string_view id, bool run);
  // The Peek page or Chromium's find bar on it has focus: page commands act on
  // the Peek, not on the tab under it (PLAN 3.8).
  bool TakesPageCommands();
  // peek.animationDone: stale numbers are ignored.
  void AnimationDone(int seq);
  // The Peek page has keyboard focus: page commands act on it (PLAN 3.8).
  bool HasFocus();
  // The page rect moved (a resize, the sidebar): the card follows.
  void Relayout();

  // windows[id].peek, and the under-peek overlay's "peek" surface (open, or
  // closing with its frame).
  std::optional<PeekSnapshot> Snapshot();
  std::optional<OverlaySurfaceSnapshot> Surface();

  // content::WebContentsDelegate (the Peek page):
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  void CloseContents(content::WebContents* source) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // content::WebContentsObserver (the Peek page):
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void DocumentOnLoadCompletedInPrimaryMainFrame() override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;

  // Tests: whether the page is attached at the card.
  bool attached_for_testing() const { return attached_; }

 private:
  struct Closing {
    int seq = 0;
    gfx::Rect origin_rect;
    gfx::Rect card_rect;
  };

  // Shows `page` as the Peek (a new seq, the card replays from the origin).
  void Show(const std::string& source_row_id,
            std::unique_ptr<content::WebContents> page);
  void Attach();
  void FinishClose();
  // A tabless page gets what a tab's page has for find, zoom and permission
  // prompts (TabFeatures::Init, as DocumentPipHost does for its page), and the
  // window as its embedding context, which the prompt bubbles anchor to.
  void ConfigurePage(content::WebContents* page);
  // Before the page leaves the Peek: Chromium's find bar goes back to the
  // active tab, and the embedding context goes (a tab sets its own).
  // `window_closing`: the window is torn down, nothing gets focus.
  void ReleasePage(bool window_closing);
  // Chromium's find bar tracks the Peek page (shown or not): it must let go
  // before the page goes.
  bool FindBarTracksPage();
  // ...and shows: page commands follow it to the Peek.
  bool FindBarOnPage();
  // The window closes: Chromium's find bar is torn down before this Peek, so
  // it lets go of the page now.
  void OnBrowserDidClose(BrowserWindowInterface* browser);
  // Cmd+F, Cmd+G and Cmd+Shift+G in the Peek: Chromium's find bar, on the Peek
  // page.
  void Find(bool find_next, bool forward);
  // Takes the page out of the Peek view, for a tab or a split.
  std::unique_ptr<content::WebContents> TakePage();
  void PostClose();
  // Where the page sits: the card, below the summary strip once there is a summary.
  gfx::Rect PageBounds() const;
  void OnPageText(int seq, base::Value text);
  void OnSummary(int seq, std::optional<std::string> answer);

  const raw_ref<CluWindowState> window_;
  std::unique_ptr<content::WebContents> page_;
  std::string source_row_id_;
  std::string instance_id_;
  int seq_ = 0;
  bool attached_ = false;
  bool committed_ = false;
  bool want_summary_ = false;
  bool from_hover_ = false;
  std::optional<std::string> summary_;
  gfx::Rect origin_rect_;
  gfx::Rect card_rect_;
  std::optional<Closing> closing_;
  base::OneShotTimer attach_timer_;
  base::OneShotTimer close_timer_;
  base::CallbackListSubscription browser_did_close_subscription_;
  base::WeakPtrFactory<CluPeek> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_PEEK_H_
