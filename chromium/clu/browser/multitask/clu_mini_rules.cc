// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_mini_rules.h"

#include <algorithm>
#include <string>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "clu/browser/ui/webui/bridge/validation.h"

namespace clu {

bool AcceptMiniUrl(const GURL& url) {
  if (!url.is_valid() || url.spec().size() > kMaxUrlLength) {
    return false;
  }
  return url.SchemeIsHTTPOrHTTPS() || url.IsAboutBlank();
}

MiniRoute RouteMiniLink(size_t open) {
  return open < kMaxMiniWindows ? MiniRoute::kNewMiniWindow
                                : MiniRoute::kTodayTab;
}

std::string MiniCapToast(size_t open) {
  return base::StrCat({"Opened in a tab: ", base::NumberToString(open),
                       " Mini windows are open"});
}

std::string MiniConfirmText(std::string_view space_name) {
  return base::StrCat({space_name,
                       " uses a different profile. Logins and form data won't "
                       "carry over."});
}

gfx::Rect MiniWindowBounds(const gfx::Rect& work_area, size_t index) {
  const int width = std::min(kMiniSize.width(),
                             std::max(kMiniMinSize.width(), work_area.width()));
  const int height = std::min(
      kMiniSize.height(), std::max(kMiniMinSize.height(), work_area.height()));
  const int offset =
      kMiniCascade * static_cast<int>(std::min<size_t>(index, kMaxMiniWindows));
  int x = work_area.x() + (work_area.width() - width) / 2 + offset;
  int y = work_area.y() + (work_area.height() - height) / 2 + offset;
  // Kept on screen: a cascade that would leave the work area stops at its
  // bottom right edge.
  x = std::clamp(x, work_area.x(),
                 std::max(work_area.x(), work_area.right() - width));
  y = std::clamp(y, work_area.y(),
                 std::max(work_area.y(), work_area.bottom() - height));
  return gfx::Rect(x, y, width, height);
}

gfx::Rect MiniPromptBounds(const gfx::Rect& work_area, size_t index) {
  const int width = std::min(kMiniPromptSize.width(), work_area.width());
  const int height = std::min(kMiniPromptSize.height(), work_area.height());
  const int offset =
      kMiniCascade * static_cast<int>(std::min<size_t>(index, kMaxMiniWindows));
  int x = work_area.right() - kMiniPromptInset - width - offset;
  int y = work_area.y() + kMiniPromptInset + offset;
  x = std::clamp(x, work_area.x(),
                 std::max(work_area.x(), work_area.right() - width));
  y = std::clamp(y, work_area.y(),
                 std::max(work_area.y(), work_area.bottom() - height));
  return gfx::Rect(x, y, width, height);
}

gfx::Rect MiniGrownBounds(const gfx::Rect& prompt, const gfx::Rect& work_area) {
  const int width = std::min(kMiniSize.width(), work_area.width());
  const int height = std::min(kMiniSize.height(), work_area.height());
  int x = prompt.right() - width;
  int y = prompt.y();
  x = std::clamp(x, work_area.x(),
                 std::max(work_area.x(), work_area.right() - width));
  y = std::clamp(y, work_area.y(),
                 std::max(work_area.y(), work_area.bottom() - height));
  return gfx::Rect(x, y, width, height);
}

gfx::Rect MiniPageRect(const gfx::Size& size, bool confirming) {
  const int top = kMiniToolbarHeight + (confirming ? kMiniConfirmHeight : 0);
  return gfx::Rect(kMiniInset, top, std::max(0, size.width() - kMiniInset * 2),
                   std::max(0, size.height() - top - kMiniInset));
}

bool MiniIdleExpired(base::TimeTicks last_used, base::TimeTicks now) {
  return !last_used.is_null() && now - last_used >= kMiniIdle;
}

}  // namespace clu
