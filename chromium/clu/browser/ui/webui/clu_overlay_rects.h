// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_OVERLAY_RECTS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_OVERLAY_RECTS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "clu/browser/model/clu_live_feed.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

// Where the passive overlay surfaces sit and how big they are, so the overlay WebUI draws each card at its view's
// origin and exactly its size: the toast stack, the Ctrl+Tab switcher and the tab hover card (hoverCardModel in
// src/shared/tabs.ts). Pure; window coordinates.
namespace clu {

struct OverlaySurfaceSnapshot;

inline constexpr int kToastWidth = 320;
inline constexpr int kToastRow = 44;
inline constexpr int kToastGap = 8;
// One toast at a time: a new toast replaces the one showing.
inline constexpr size_t kMaxToasts = 1;
// The toast sits inside the page card's top right corner, 6 points in from the card's right edge and 8
// below its top (16 and 18 from the window's at the default layout). The view adds 8 points around the toast for its
// shadow (the overlay's .clu-toasts padding).
inline constexpr int kToastInsetRight = 6;
inline constexpr int kToastInsetTop = 8;
inline constexpr int kToastPad = 8;

// The toast: at the page card's top right, sized to `count` toasts (at most kMaxToasts). It is placed from the card,
// never the window, so every toast stays inside the card in toolbar mode, focus mode and full screen too.
gfx::Rect ToastRect(const gfx::Rect& card, size_t count);

// Centered, at most 960x170; narrower windows shrink the cards to fit.
gfx::Rect SwitcherRect(const gfx::Size& content, size_t count);

struct HoverCardInput {
  HoverCardInput();
  HoverCardInput(const HoverCardInput&);
  HoverCardInput& operator=(const HoverCardInput&);
  ~HoverCardInput();

  std::string title;
  std::string title_override;
  std::string url;
  std::string pinned_url;
  bool off_pinned = false;
  bool audible = false;
  bool muted = false;
  // "live", "unloaded" or "hibernated".
  std::string lifecycle = "live";
  // A Gmail row's unread mail from its account's Atom feed; nullopt when the feed gave no answer.
  std::optional<GmailUnread> mail;
};

struct HoverCardModel {
  HoverCardModel();
  HoverCardModel(const HoverCardModel&);
  HoverCardModel& operator=(const HoverCardModel&);
  ~HoverCardModel();

  std::string title;
  int title_lines = 1;
  std::string location;
  // Set while the tab is off its pinned URL.
  std::string pinned;
  std::vector<std::string> chips;
  int width = 0;
  int height = 0;
  // "title" (the pill) or "preview": a preview site's card, loaded or asleep, which Hover fills with the site's live
  // compact page.
  std::string kind = "title";
  int unread = 0;
  // The mail card's source: "feed" (the account's Atom feed: the count and the newest messages), "title" (the count
  // in the tab's title, the feed unavailable) or "unknown" (neither: an asleep row whose stored title has no count).
  std::string mail_source;
  // The feed's newest unread messages, newest first, at most kMaxGmailUnreadItems; empty unless the source is "feed".
  std::vector<GmailUnreadItem> mail_items;
  // The preview site's name ("Google Calendar"), empty for the pill.
  std::string site;
};

// The preview site a host belongs to ("Gmail", "Google Calendar", "Google Meet", ...), empty for any other host
// (previewSite() in src/shared/tabs.ts).
std::string PreviewSite(std::string_view host);
// The compact page the preview card shows live for `page_url`, on the account (/u/N/) the tab is signed in
// as: Calendar's day view (it opens at the current time), Gmail's inbox, Meet's landing page. Empty for any other
// page.
std::string LivePreviewUrl(std::string_view page_url);
// The phone user agent the card's own page loads with, so each site serves its narrow view (Calendar's day, Meet's compact
// page) instead of its desktop page cropped to the card. The tab keeps its own.
std::string LivePreviewUserAgent();
// Where that page sits: the preview card's 239 x 256 inside its view (`card_view`, the shadow margin around it).
gfx::Rect LivePreviewPageRect(const gfx::Rect& card_view);

// Gmail's card, 239 wide; 64 tall for one or two lines ("No unread emails, you can exhale!"), else a
// 30 tall header and a 44 tall row per message inside 6 points of padding (mailCardHeight() in src/shared/tabs.ts).
inline constexpr int kMailCardWidth = 239;
inline constexpr int kMailCardShortHeight = 64;
inline constexpr int kMailCardHeader = 30;
inline constexpr int kMailCardRow = 44;
inline constexpr int kMailCardPad = 6;
int MailCardHeight(size_t rows);

// The unread count in a page title, "(3) Inbox"; 0 without one (unreadCount() in src/shared/tabs.ts).
int UnreadCount(std::string_view title);

HoverCardModel BuildHoverCard(const HoverCardInput& input);
// Hung 11 right of and 12 below the pointer (the anchor's origin; the frame sends a zero-size rect at the pointer),
// less the view's shadow margin, kept inside the window. hoverCardRect() in src/shared/tabs.ts.
gfx::Rect HoverCardRect(const gfx::Size& content,
                        const gfx::Rect& anchor,
                        const HoverCardModel& model);

// The part of a `content`-sized window the top overlay needs for `surfaces`, its top-layer surfaces besides the
// command bar (docs/chromium/fixes.md; no TS twin): while every one is a passive surface with a rect (the tweak editor
// docked over the page card), the union of their rects inside the window, so the page and the sidebar beside them
// take clicks. Nullopt, the whole window, for anything else: a prompt, a menu, a surface without a rect, none.
std::optional<gfx::Rect> PassiveTopOverlayRect(
    const gfx::Size& content,
    const std::vector<OverlaySurfaceSnapshot>& surfaces);

// middleTruncate() and hostAndPath() of src/shared/tabs.ts.
std::string MiddleTruncate(const std::string& text, size_t max);
std::string HostAndPath(const std::string& url);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_OVERLAY_RECTS_H_
