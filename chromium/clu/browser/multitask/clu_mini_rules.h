// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MULTITASK_CLU_MINI_RULES_H_
#define CLU_BROWSER_MULTITASK_CLU_MINI_RULES_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

// Mini window rules (docs/plan/05-multitask.md "Mini window", PLAN decisions
// 27, 37 and 54, src/shared/multitask.ts). Pure, so clu_unittests covers them.
namespace clu {

// MAX_MINI_WINDOWS, MINI_SIZE, MINI_MIN_SIZE, MINI_PROMPT_SIZE,
// MINI_PROMPT_INSET, MINI_CASCADE, MINI_TOOLBAR_H, MINI_INSET, MINI_CONFIRM_H
// and MINI_IDLE_MS in src/shared/multitask.ts.
inline constexpr size_t kMaxMiniWindows = 6;
inline constexpr gfx::Size kMiniSize(880, 620);
// The minimum lets the empty prompt be its own size.
inline constexpr gfx::Size kMiniMinSize(480, 329);
// The empty prompt (Cmd+Opt+N): a 490 x 329 window at the
// screen's top-right, 20 in from both edges.
inline constexpr gfx::Size kMiniPromptSize(490, 329);
inline constexpr int kMiniPromptInset = 20;
inline constexpr int kMiniCascade = 22;
inline constexpr int kMiniToolbarHeight = 36;
inline constexpr int kMiniInset = 8;
inline constexpr int kMiniConfirmHeight = 40;
inline constexpr base::TimeDelta kMiniIdle = base::Hours(6);
// How often an open Mini window checks whether it has gone idle.
inline constexpr base::TimeDelta kMiniIdleCheck = base::Minutes(1);

// A URL a Mini window opens (acceptMiniUrl): http, https and about:blank, at
// most 32 KiB. Anything else (file:, javascript:, data:, chrome:, junk) is
// refused and the link is dropped.
bool AcceptMiniUrl(const GURL& url);

// What a link from another app does with `open` Mini windows already on screen
// (decision 27): a new Mini window up to the cap, then a Today tab at the top
// of the last focused main window's space with a toast.
enum class MiniRoute { kNewMiniWindow, kTodayTab };
MiniRoute RouteMiniLink(size_t open);

// The toast that names the cap ("Opened in a tab: 6 Mini windows are open",
// miniCapToast).
std::string MiniCapToast(size_t open);
// The confirm strip's text for a space of another profile (miniConfirmText).
std::string MiniConfirmText(std::string_view space_name);

// Where the `index`th Mini window (0 for the first open one) opens: 880 by 620,
// centered in `work_area`, each further one 22 DIP down and right, kept inside
// the work area (05 "The window").
gfx::Rect MiniWindowBounds(const gfx::Rect& work_area, size_t index);

// Where the empty prompt of the `index`th Mini window opens: 490 by 329, 20 in
// from the work area's top-right corner, each further one 22 DIP down and left,
// kept inside the work area.
gfx::Rect MiniPromptBounds(const gfx::Rect& work_area, size_t index);

// The prompt's window once its page loads: 880 by 620 (at most the work area),
// keeping its top-right corner, kept inside the work area.
gfx::Rect MiniGrownBounds(const gfx::Rect& prompt, const gfx::Rect& work_area);

// The page view inside a Mini window of `size` (miniPageRect): under the 36 DIP
// toolbar, inset 8, pushed down by the confirm strip while it shows.
gfx::Rect MiniPageRect(const gfx::Size& size, bool confirming);

// A Mini window nobody has used for 6 hours closes and archives its page
// (decision 54).
bool MiniIdleExpired(base::TimeTicks last_used, base::TimeTicks now);

}  // namespace clu

#endif  // CLU_BROWSER_MULTITASK_CLU_MINI_RULES_H_
