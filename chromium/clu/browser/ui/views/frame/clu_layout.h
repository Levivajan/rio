// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_VIEWS_FRAME_CLU_LAYOUT_H_
#define CLU_BROWSER_UI_VIEWS_FRAME_CLU_LAYOUT_H_

#include <cstdint>
#include <optional>

#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

// Shell layout and sidebar tween math, ported from src/shared/shell/layout.ts and src/shared/shell/tween.ts. The
// frame WebUI draws the page card at the rect this computes; clu_layout_unittest.cc carries the TS test cases.
namespace clu {

inline constexpr int kSidebarWidth = 214;
inline constexpr int kSidebarMinWidth = 164;
inline constexpr int kSidebarMaxWidth = 500;
// The card's inset and radius come from the look tokens (M11, src/shared/look/tokens.ts).
inline constexpr int kCardInset = look::kCardInset;
inline constexpr float kCardRadius = look::kCardRadius;
inline constexpr int kFocusGutter = 4;
// Toolbar mode's band above the card (TOOLBAR_H in layout.ts).
inline constexpr int kToolbarHeight = 38;
// The hidden sidebar's reveal zone: the pointer within 5 of the window's left edge; x 7 does not reveal.
// PEEK_ZONE_X in src/shared/shell/peek.ts.
inline constexpr int kPeekZoneX = 5;
// Room the traffic lights take before the sidebar toggle (TRAFFIC_SPACER in layout.ts).
inline constexpr int kTrafficSpacer = 66;
// Room the page card keeps next to the sidebar before the sidebar gives way on a narrow window.
inline constexpr int kMinPageRoom = 360;
// The smallest window the shell lays out: a 200 px sidebar beside 360 px of page.
inline constexpr int kMinWindowWidth = 560;
inline constexpr int kMinWindowHeight = 400;

// Showing the sidebar docks it in 85 ms and hiding it takes 80 (sidebarTweenMs in tween.ts).
inline constexpr int kSidebarShowMs = look::kSidebarShowMs;
inline constexpr int kSidebarHideMs = look::kSidebarHideMs;
// The Library widens the sidebar to kLibraryWidth and narrows it back over kLibraryMs, exponential with a
// kLibraryTauMs time constant (LIBRARY_TWEEN in tween.ts).
inline constexpr int kLibraryMs = 290;
inline constexpr int kLibraryTauMs = 42;

// ChromeState in layout.ts.
struct ChromeState {
  int sidebar_width = kSidebarWidth;
  bool sidebar_hidden = false;
  bool focus_mode = false;
  bool fullscreen = false;
  // A page (a video) or an extension took the window full screen: the page fills it, nothing else shows.
  bool content_fullscreen = false;
  // The Library is open: the docked sidebar widens to kLibraryWidth (LIBRARY_WIDTH in layout.ts).
  bool library_open = false;
  // The width the Library's section asks for (libraryWidth in layout.ts); below kLibraryWidth it is ignored.
  int library_width = 0;
  // Toolbar mode (View > Show Toolbar, Cmd+Shift+D): the card moves down under the toolbar's band.
  bool toolbar = false;
  // The front page is in Developer Mode: its strip takes the toolbar's band.
  bool dev_strip = false;
};

inline constexpr int kLibraryWidth = 417;

// ShellLayout in layout.ts. sidebar_rect keeps the sidebar's size while it is hidden: the slide and the hover peek
// draw it at that width.
struct ShellLayout {
  gfx::Size content_size;
  gfx::Rect page_rect;
  gfx::Rect sidebar_rect;
  std::optional<gfx::Rect> hot_zone;
  float radius = kCardRadius;
  // Width the sidebar actually takes: the stored width clamped for this window size.
  int effective_width = kSidebarWidth;
  // Width of the traffic-light spacer at the start of the top row, kept in full screen.
  int traffic_spacer = kTrafficSpacer;
  // Whether the window buttons belong on screen (sidebar shown).
  bool traffic_lights = true;

  bool operator==(const ShellLayout&) const = default;
};

// The stored width as a usable number: junk falls back to the default, then the 164..500 range applies.
int StoredSidebarWidth(double width);

// The stored width clamped to what this window can give: at most 45% of it and never under 360 px of page.
int EffectiveSidebarWidth(int stored, int window_width);

ShellLayout ComputeShellLayout(const gfx::Size& content_size,
                               const ChromeState& chrome);

// SidebarAnim in tween.ts. `from` and `to` are the page card's left edge, which is also the sliding sidebar's right
// edge. `start_at_ms` is milliseconds since the Unix epoch, the clock the frame WebUI reads with Date.now().
struct SidebarAnim {
  int from = 0;
  int to = 0;
  int64_t start_at_ms = 0;
  int ms = 0;
  gfx::Rect from_rect;
  gfx::Rect to_rect;
  // The Library's exponential curve ("exp" in state); else the dock's ease-out cubic.
  bool exponential = false;

  bool operator==(const SidebarAnim&) const = default;
};

// Where Chromium's downloads bubble hangs (docs/chromium/fixes.md; no TS twin): the sidebar's downloads button at
// `button`, the rect the frame WebUI last reported, while the docked sidebar shows it (no tween running, neither
// hidden nor in focus mode) and it lies inside the sidebar; else none, and the bubble hangs from the page card.
std::optional<gfx::Rect> DownloadsButtonAnchor(
    const ShellLayout& layout,
    const std::optional<gfx::Rect>& button,
    bool tweening);

// The ease of one tween: the dock's ease-out cubic, the Library's exponential ease-out (kLibraryTauMs), or the
// hide's sampled curve (look::kSidebarHideCurve).
enum class TweenCurve { kCubic, kExp, kHide };

// Eased progress at `t` (0 to 1 of a tween of `ms`) on `curve`: 0 at or before the start, exactly 1 at the end.
double EaseAt(double t, int ms, TweenCurve curve = TweenCurve::kCubic);

// The curve `anim` runs on: the Library's exponential both ways, any other hide on the hide's curve, else the dock's.
TweenCurve CurveOf(const SidebarAnim& anim);

// Eased progress of `anim` at `now_ms` (milliseconds since the Unix epoch, fractional), read from (now - start) / ms
// on every display frame, never on a fixed 16 ms tick, so the card moves at the display's rate (120 Hz on ProMotion).
// 0 before the start, 1 at the end or when motion is reduced (ms 0). progressAt in tween.ts.
double TweenProgress(const SidebarAnim& anim, double now_ms);

// The page rect at `progress`, rounded to whole DIPs.
gfx::Rect LerpRect(const gfx::Rect& from, const gfx::Rect& to, double progress);

}  // namespace clu

#endif  // CLU_BROWSER_UI_VIEWS_FRAME_CLU_LAYOUT_H_
