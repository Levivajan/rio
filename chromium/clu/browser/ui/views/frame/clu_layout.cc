// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/views/frame/clu_layout.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "base/containers/span.h"

namespace clu {

namespace {

// Math.round() in JS: halves round up.
int RoundHalfUp(double value) {
  return static_cast<int>(std::floor(value + 0.5));
}

int NonNegative(int value) {
  return std::max(0, value);
}

double EaseOutCubic(double t) {
  return 1 - std::pow(1 - t, 3);
}

// easeOutExp in tween.ts: 1 - e^(-t / tau), scaled to land on 1 at the end of a tween of `ms`.
double EaseOutExp(double t, int ms) {
  const double end = 1 - std::exp(-static_cast<double>(ms) / kLibraryTauMs);
  return (1 - std::exp(-t * ms / kLibraryTauMs)) / end;
}

// look::kSidebarHideCurve at `t` (0 to 1 of the tween), linear between samples (hideEase in tween.ts).
double HideEase(double t) {
  const base::span<const look::CurvePoint> curve(look::kSidebarHideCurve);
  const double end = curve.back().ms;
  const double ms = std::clamp(t, 0.0, 1.0) * end;
  for (size_t i = 1; i < curve.size(); ++i) {
    const look::CurvePoint& a = curve[i - 1];
    const look::CurvePoint& b = curve[i];
    if (ms <= b.ms) {
      return b.ms > a.ms ? a.progress + (b.progress - a.progress) *
                                            (ms - a.ms) / (b.ms - a.ms)
                         : b.progress;
    }
  }
  return 1;
}

}  // namespace

int StoredSidebarWidth(double width) {
  if (!std::isfinite(width)) {
    return kSidebarWidth;
  }
  return std::clamp(RoundHalfUp(std::clamp(width, -1e6, 1e6)),
                    kSidebarMinWidth, kSidebarMaxWidth);
}

int EffectiveSidebarWidth(int stored, int window_width) {
  const int width = NonNegative(window_width);
  const int max =
      std::max(kSidebarMinWidth,
               std::min({kSidebarMaxWidth,
                         static_cast<int>(std::floor(width * 0.45)),
                         width - kMinPageRoom}));
  return std::min(max, StoredSidebarWidth(stored));
}

ShellLayout ComputeShellLayout(const gfx::Size& content_size,
                               const ChromeState& chrome) {
  const int width = NonNegative(content_size.width());
  const int height = NonNegative(content_size.height());
  ShellLayout layout;
  layout.content_size = gfx::Size(width, height);
  const int docked = EffectiveSidebarWidth(chrome.sidebar_width, width);
  const int library =
      chrome.library_open
          ? std::min(std::max(kLibraryWidth, chrome.library_width),
                     NonNegative(width - kMinPageRoom))
          : 0;
  layout.effective_width = std::max(docked, library);
  // Full screen keeps the spacer: macOS reveals the window buttons there when the pointer reaches the top, so the
  // controls stay after them and never jump when they hide.
  layout.traffic_spacer = kTrafficSpacer;
  layout.sidebar_rect = gfx::Rect(0, 0, layout.effective_width, height);

  // A page that goes full screen fills the screen, as in any browser: no sidebar, no inset, square corners, no reveal.
  if (chrome.fullscreen && chrome.content_fullscreen) {
    layout.page_rect = gfx::Rect(0, 0, width, height);
    layout.hot_zone = std::nullopt;
    layout.radius = 0;
    layout.traffic_lights = false;
    return layout;
  }

  if (chrome.focus_mode) {
    layout.page_rect = gfx::Rect(kFocusGutter, 0,
                                 NonNegative(width - kFocusGutter), height);
    layout.hot_zone = gfx::Rect(0, 0, std::min(kFocusGutter, width), height);
    layout.radius = 0;
    layout.traffic_lights = false;
    return layout;
  }

  // Toolbar mode and the Developer Mode strip move the card down under the band (focus mode has neither).
  const int top =
      kCardInset + (chrome.toolbar || chrome.dev_strip ? kToolbarHeight : 0);
  const int inner_height = NonNegative(height - top - kCardInset);
  if (chrome.sidebar_hidden) {
    layout.page_rect = gfx::Rect(kCardInset, top,
                                 NonNegative(width - kCardInset * 2),
                                 inner_height);
    // x 0 to kPeekZoneX inclusive: the rect's right edge is exclusive.
    layout.hot_zone =
        gfx::Rect(0, 0, std::min(kPeekZoneX + 1, width), height);
    layout.radius = kCardRadius;
    layout.traffic_lights = false;
    return layout;
  }

  // The sidebar's own right padding supplies the gap on the card's left.
  layout.page_rect = gfx::Rect(
      layout.effective_width, top,
      NonNegative(width - layout.effective_width - kCardInset), inner_height);
  layout.hot_zone = std::nullopt;
  layout.radius = kCardRadius;
  layout.traffic_lights = true;
  return layout;
}

double EaseAt(double t, int ms, TweenCurve curve) {
  if (!(t > 0)) {
    return 0;
  }
  if (t >= 1 || ms <= 0) {
    return 1;
  }
  switch (curve) {
    case TweenCurve::kExp:
      return EaseOutExp(t, ms);
    case TweenCurve::kHide:
      return HideEase(t);
    case TweenCurve::kCubic:
      return EaseOutCubic(t);
  }
  return EaseOutCubic(t);
}

TweenCurve CurveOf(const SidebarAnim& anim) {
  return anim.exponential      ? TweenCurve::kExp
         : anim.to < anim.from ? TweenCurve::kHide
                               : TweenCurve::kCubic;
}

double TweenProgress(const SidebarAnim& anim, double now_ms) {
  if (anim.ms <= 0) {
    return 1;
  }
  const double t = (now_ms - static_cast<double>(anim.start_at_ms)) / anim.ms;
  // A NaN clock (never expected) lands the tween instead of freezing it.
  return std::isnan(t) ? 1 : EaseAt(t, anim.ms, CurveOf(anim));
}

gfx::Rect LerpRect(const gfx::Rect& from, const gfx::Rect& to,
                   double progress) {
  if (progress >= 1) {
    return to;
  }
  const auto lerp = [progress](int a, int b) {
    return RoundHalfUp(a + (b - a) * progress);
  };
  return gfx::Rect(lerp(from.x(), to.x()), lerp(from.y(), to.y()),
                   NonNegative(lerp(from.width(), to.width())),
                   NonNegative(lerp(from.height(), to.height())));
}

std::optional<gfx::Rect> DownloadsButtonAnchor(
    const ShellLayout& layout,
    const std::optional<gfx::Rect>& button,
    bool tweening) {
  if (!button || tweening || !layout.traffic_lights || button->IsEmpty() ||
      !layout.sidebar_rect.Contains(*button)) {
    return std::nullopt;
  }
  return *button;
}

}  // namespace clu
