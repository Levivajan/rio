// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MULTITASK_CLU_PEEK_RULES_H_
#define CLU_BROWSER_MULTITASK_CLU_PEEK_RULES_H_

#include <optional>
#include <string>
#include <string_view>

#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

// When a link opens in Peek, and where the card sits (docs/plan/05-multitask.md
// "Peek", PLAN decision 50). Pure, so clu_unittests covers it.
namespace clu {

// The row a link came from.
enum class PeekSource { kPinned, kFavorite, kNormal };

// How the request reached Chromium.
enum class PeekVia {
  // A same-tab navigation the page started (the navigation throttle).
  kNavigate,
  // WebContentsDelegate::OpenURLFromTab: a modified click (Shift opens a new
  // window, Cmd a background tab).
  kOpenUrl,
  // WebContentsDelegate::AddNewContents: target=_blank or window.open, with a
  // WebContents the page made.
  kNewContents,
};

// The dispositions the decision tells apart (ui::WindowOpenDisposition, mapped
// by the caller).
enum class PeekDisposition {
  kCurrentTab,
  kForegroundTab,
  kBackgroundTab,
  kNewWindow,
  kNewPopup,
  kOther,
};

enum class PeekDecision {
  // Open the target in the window's Peek.
  kPeek,
  // Leave the request to Chromium.
  kChromium,
};

struct PeekInput {
  PeekSource source = PeekSource::kNormal;
  GURL source_url;
  GURL target_url;
  PeekVia via = PeekVia::kNavigate;
  PeekDisposition disposition = PeekDisposition::kCurrentTab;
  // A main-frame link click the page let through with a user gesture
  // (NavigationHandle::WasInitiatedByLinkClick and HasUserGesture).
  bool link_click = false;
  // The request carries a body (a form POST). Never peeked: a Peek replays as
  // GET.
  bool post = false;
  bool form_submission = false;
  // window.open asked for a size or a popup: it stays a popup (05 "Peek"
  // rules).
  bool sized = false;
  // The browser opened it, not a click in the page: the context menu's Open
  // Link in New Window, a menu or a command. It opens what was asked.
  bool from_browser = false;
  // Settings, Links (docs/chromium/parity-settings.md): links.peekOnShiftClick (a Shift-click, or a page's new
  // window) and links.peekOtherSites (a pinned tab's or favorite's link to another site). Off, Chromium opens it.
  bool peek_on_shift_click = true;
  bool peek_other_sites = true;
};

// http and https only: Clu's normal URL policy handles every other scheme.
bool IsPeekableUrl(const GURL& url);

// The host for the Peek comparison: lowercase as GURL gives it (punycode), one
// leading "www." dropped. Empty for a URL that is not http(s) or has no host.
std::string PeekHost(const GURL& url);

// Two http(s) URLs whose hosts differ after dropping one leading "www."
// (decision 50): any other difference opens a Peek, so subdomains, IP hosts and
// localhost count, and the 127.0.0.1 and localhost pair of the fixture server
// is cross-site.
bool HostsDiffer(const GURL& a, const GURL& b);

// The 05 table, on Chromium's inputs:
// - pinned or favorite source, plain cross-host link click, no form, no POST:
// Peek;
// - Shift-click (kNewWindow from OpenURLFromTab or a target=_blank link) in any
// tab: Peek;
// - target=_blank or window.open without size features from a pinned or
// favorite source to another host: Peek;
// - everything else (same host, Cmd-clicks, middle clicks, sized popups, form
// POSTs, other schemes): Chromium.
PeekDecision ShouldPeek(const PeekInput& input);

// The card: centered in the page rect, min(1100, 86%) wide, 88% high, at least
// 360 by 240 (05 "Bounds").
gfx::Rect PeekCardRect(const gfx::Rect& page_rect);

// The Peek's host label (windows[id].peek.host): the hostname as the URL has
// it.
std::string PeekDisplayHost(const GURL& url);

// Timings (05 "Timings").
inline constexpr int kPeekAttachFallbackMs = 250;
inline constexpr int kPeekCloseFallbackMs = 250;

}  // namespace clu

#endif  // CLU_BROWSER_MULTITASK_CLU_PEEK_RULES_H_
