// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_peek_rules.h"

#include <algorithm>
#include <cmath>

#include "base/strings/string_util.h"
#include "clu/browser/ui/views/frame/clu_look_tokens.h"
#include "url/url_constants.h"

namespace clu {

namespace {

constexpr int kMaxCardWidth = 1100;
// The card is 853 wide over a 1056 page card at a 1280 window.
constexpr double kCardWidthShare = 0.808;
constexpr int kMinCardWidth = 360;
constexpr int kMinCardHeight = 240;

bool Anchored(PeekSource source) {
  return source == PeekSource::kPinned || source == PeekSource::kFavorite;
}

}  // namespace

bool IsPeekableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.host().empty();
}

std::string PeekHost(const GURL& url) {
  if (!IsPeekableUrl(url)) {
    return std::string();
  }
  std::string host = base::ToLowerASCII(url.host());
  if (host.starts_with("www.")) {
    host.erase(0, 4);
  }
  return host;
}

bool HostsDiffer(const GURL& a, const GURL& b) {
  const std::string ha = PeekHost(a);
  const std::string hb = PeekHost(b);
  return !ha.empty() && !hb.empty() && ha != hb;
}

PeekDecision ShouldPeek(const PeekInput& input) {
  if (!IsPeekableUrl(input.target_url) || input.post || input.form_submission) {
    return PeekDecision::kChromium;
  }
  const bool anchored_elsewhere = input.peek_other_sites &&
                                  Anchored(input.source) &&
                                  HostsDiffer(input.source_url, input.target_url);
  switch (input.via) {
    case PeekVia::kNavigate:
      // Only a plain click the page let through: script redirects and forms
      // navigate normally.
      return anchored_elsewhere && input.link_click &&
                     input.disposition == PeekDisposition::kCurrentTab
                 ? PeekDecision::kPeek
                 : PeekDecision::kChromium;
    case PeekVia::kOpenUrl:
      // Shift-click; Cmd and middle clicks keep their tabs, and the context
      // menu's Open Link in New Window opens a window.
      return input.peek_on_shift_click &&
                     input.disposition == PeekDisposition::kNewWindow &&
                     !input.sized && !input.from_browser
                 ? PeekDecision::kPeek
                 : PeekDecision::kChromium;
    case PeekVia::kNewContents:
      if (input.sized || input.disposition == PeekDisposition::kNewPopup) {
        return PeekDecision::kChromium;
      }
      if (input.disposition == PeekDisposition::kNewWindow) {
        return input.peek_on_shift_click ? PeekDecision::kPeek
                                         : PeekDecision::kChromium;
      }
      return input.disposition == PeekDisposition::kForegroundTab &&
                     anchored_elsewhere
                 ? PeekDecision::kPeek
                 : PeekDecision::kChromium;
  }
  return PeekDecision::kChromium;
}

gfx::Rect PeekCardRect(const gfx::Rect& page_rect) {
  const int width = std::max(
      kMinCardWidth,
      std::min(kMaxCardWidth, static_cast<int>(std::lround(page_rect.width() *
                                                           kCardWidthShare))));
  // The card spans the window's height less the inset top and bottom; the page card already sits
  // page_rect.y() below the window top, so only the rest of the inset is taken from the page.
  const int inset_y = std::max(0, look::kPeekInsetY - page_rect.y());
  const int height =
      std::max(kMinCardHeight, page_rect.height() - 2 * inset_y);
  const int x = static_cast<int>(
      std::lround(page_rect.x() + (page_rect.width() - width) / 2.0));
  const int y = static_cast<int>(
      std::lround(page_rect.y() + (page_rect.height() - height) / 2.0));
  return gfx::Rect(x, y, width, height);
}

std::string PeekDisplayHost(const GURL& url) {
  return url.is_valid() ? std::string(url.host()) : std::string();
}

}  // namespace clu
