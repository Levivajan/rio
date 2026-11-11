// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_overlay_rects.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/third_party/icu/icu_utf.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr int kSwitcherCardWidth = 176;
constexpr int kSwitcherGap = 10;
constexpr int kSwitcherInset = 12 + 8;
constexpr int kSwitcherMaxWidth = 960;
constexpr int kSwitcherHeight = 170;
constexpr size_t kSwitcherMaxCards = 5;

// HOVER_CARD in src/shared/tabs.ts: the title pill, hung from the pointer, in a view with a shadow margin.
constexpr int kHoverPill = 28;
constexpr int kHoverMaxPill = 320;
constexpr int kHoverMargin = 8;
// LIVE_PREVIEW in src/shared/tabs.ts.
constexpr int kLivePreviewWidth = 239;
constexpr int kLivePreviewHeight = 256;
constexpr int kHoverOffsetX = 11;
constexpr int kHoverOffsetY = 12;
constexpr size_t kHoverLocationChars = 44;
constexpr size_t kHoverTitleMax = 300;

// Code points, the way Array.from() splits a JS string.
std::vector<std::u16string> CodePointsOf(std::u16string_view text) {
  std::vector<std::u16string> out;
  size_t i = 0;
  while (i < text.size()) {
    size_t length = 1;
    if (CBU16_IS_LEAD(text[i]) && i + 1 < text.size() &&
        CBU16_IS_TRAIL(text[i + 1])) {
      length = 2;
    }
    out.emplace_back(text.substr(i, length));
    i += length;
  }
  return out;
}

std::string CleanHoverTitle(const std::string& raw) {
  std::u16string text = base::UTF8ToUTF16(raw);
  for (char16_t& c : text) {
    if (c <= 0x1F || (c >= 0x7F && c <= 0x9F)) {
      c = u' ';
    }
  }
  std::u16string trimmed(base::TrimWhitespace(text, base::TRIM_ALL));
  if (trimmed.size() > kHoverTitleMax) {
    trimmed.resize(kHoverTitleMax);
  }
  return base::UTF16ToUTF8(trimmed);
}

}  // namespace

HoverCardInput::HoverCardInput() = default;
HoverCardInput::HoverCardInput(const HoverCardInput&) = default;
HoverCardInput& HoverCardInput::operator=(const HoverCardInput&) = default;
HoverCardInput::~HoverCardInput() = default;

HoverCardModel::HoverCardModel() = default;
HoverCardModel::HoverCardModel(const HoverCardModel&) = default;
HoverCardModel& HoverCardModel::operator=(const HoverCardModel&) = default;
HoverCardModel::~HoverCardModel() = default;

gfx::Rect ToastRect(const gfx::Rect& card, size_t count) {
  const int n = static_cast<int>(std::clamp<size_t>(count, 1, kMaxToasts));
  const int height = n * kToastRow + (n - 1) * kToastGap + 2 * kToastPad;
  const int right = card.right() - kToastInsetRight + kToastPad;
  const int left = std::max({0, card.x() - kToastPad, right - kToastWidth});
  return gfx::Rect(left, std::max(0, card.y() + kToastInsetTop - kToastPad),
                   std::max(0, right - left), height);
}

gfx::Rect SwitcherRect(const gfx::Size& content, size_t count) {
  const int n = static_cast<int>(std::clamp<size_t>(count, 1, kSwitcherMaxCards));
  const int natural =
      n * kSwitcherCardWidth + (n - 1) * kSwitcherGap + 2 * kSwitcherInset;
  const int width = std::max(
      0, std::min({kSwitcherMaxWidth, natural, content.width() - 32}));
  const int height = std::max(0, std::min(kSwitcherHeight, content.height()));
  return gfx::Rect((content.width() - width) / 2,
                   (content.height() - height) / 2, width, height);
}

std::string MiddleTruncate(const std::string& text, size_t max) {
  const std::vector<std::u16string> chars =
      CodePointsOf(base::UTF8ToUTF16(text));
  if (chars.size() <= max) {
    return text;
  }
  if (max <= 1) {
    return "…";
  }
  const size_t keep = max - 1;
  const size_t head = (keep + 1) / 2;
  std::u16string out;
  for (size_t i = 0; i < head; ++i) {
    out += chars[i];
  }
  out += u"…";
  for (size_t i = chars.size() - (keep - head); i < chars.size(); ++i) {
    out += chars[i];
  }
  return base::UTF16ToUTF8(out);
}

std::string HostAndPath(const std::string& url) {
  const std::string raw = url.substr(0, 4096);
  if (raw.starts_with("rio://")) {
    // Rio's own page by its host, as hostAndPath() shows it: rio://settings.
    return raw.substr(0, raw.find_first_of("/?#", 6));
  }
  const GURL gurl(raw);
  if (!gurl.is_valid()) {
    return raw;
  }
  if (!gurl.SchemeIsHTTPOrHTTPS()) {
    return raw;
  }
  std::string host(gurl.host());
  if (host.starts_with("www.")) {
    host = host.substr(4);
  }
  if (gurl.has_port()) {
    host = base::StrCat({host, ":", gurl.port()});
  }
  const std::string path(gurl.path());
  return path == "/" ? host : base::StrCat({host, path});
}

int MailCardHeight(size_t rows) {
  if (rows == 0) {
    return kMailCardShortHeight;
  }
  rows = std::min(rows, kMaxGmailUnreadItems);
  return 2 * kMailCardPad + kMailCardHeader +
         static_cast<int>(rows) * kMailCardRow;
}

int UnreadCount(std::string_view title) {
  title = title.substr(0, 300);
  for (size_t open = title.find('('); open != std::string_view::npos;
       open = title.find('(', open + 1)) {
    if (open > 0 && !base::IsAsciiWhitespace(title[open - 1])) {
      continue;
    }
    size_t end = open + 1;
    while (end < title.size() && end - open <= 6 &&
           base::IsAsciiDigit(title[end])) {
      ++end;
    }
    const size_t digits = end - open - 1;
    size_t close = end;
    if (close < title.size() && title[close] == '+') {
      ++close;
    }
    if (digits == 0 || close >= title.size() || title[close] != ')' ||
        (close + 1 < title.size() &&
         !base::IsAsciiWhitespace(title[close + 1]))) {
      continue;
    }
    int count = 0;
    for (size_t i = open + 1; i < end; ++i) {
      count = count * 10 + (title[i] - '0');
    }
    return count;
  }
  return 0;
}

std::string PreviewSite(std::string_view host) {
  if (host.starts_with("www.")) {
    host.remove_prefix(4);
  }
  static constexpr std::pair<std::string_view, std::string_view> kSites[] = {
      {"mail.google.com", "Gmail"},
      {"calendar.google.com", "Google Calendar"},
      {"meet.google.com", "Google Meet"},
  };
  for (const auto& [site_host, name] : kSites) {
    if (host == site_host) {
      return std::string(name);
    }
  }
  return std::string();
}

std::string LivePreviewUrl(std::string_view page_url) {
  const GURL url(page_url);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  const std::string site = PreviewSite(url.host());
  // The account index Google puts in the path ("/mail/u/1/", "/calendar/u/2/r"), or 0; anything else is 0.
  std::string account = "0";
  const std::string_view path = url.path();
  if (const size_t at = path.find("/u/"); at != std::string_view::npos) {
    const std::string_view rest = path.substr(at + 3);
    const size_t digits = rest.find_first_not_of("0123456789");
    const std::string_view index = rest.substr(0, digits);
    if (!index.empty() && index.size() <= 2) {
      account = std::string(index);
    }
  }
  if (site == "Google Calendar") {
    return base::StrCat(
        {"https://calendar.google.com/calendar/u/", account, "/r/day"});
  }
  if (site == "Google Meet") {
    return base::StrCat({"https://meet.google.com/landing?authuser=", account});
  }
  return std::string();
}

std::string LivePreviewUserAgent() {
  return "Mozilla/5.0 (iPhone; CPU iPhone OS 18_6 like Mac OS X) "
         "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.6 Mobile/15E148 "
         "Safari/604.1";
}

gfx::Rect LivePreviewPageRect(const gfx::Rect& card_view) {
  gfx::Rect page = card_view;
  page.Inset(kHoverMargin);
  return page;
}

HoverCardModel BuildHoverCard(const HoverCardInput& t) {
  HoverCardModel m;
  const std::string clean =
      CleanHoverTitle(t.title_override.empty() ? t.title : t.title_override);
  m.location = MiddleTruncate(HostAndPath(t.url), kHoverLocationChars);
  std::string host;
  const GURL gurl(t.url);
  if (gurl.is_valid() && gurl.has_host()) {
    host = std::string(gurl.host());
    if (host.starts_with("www.")) {
      host = host.substr(4);
    }
  }
  m.title = !clean.empty()            ? clean
            : !host.empty()           ? host
            : !m.location.empty()     ? m.location
                                      : std::string("Untitled");
  m.title_lines = 1;
  if (t.off_pinned && !t.pinned_url.empty()) {
    m.pinned =
        MiddleTruncate(HostAndPath(t.pinned_url), kHoverLocationChars - 8);
  }
  if (t.muted) {
    m.chips.push_back("Muted");
  } else if (t.audible) {
    m.chips.push_back("Playing");
  }
  if (t.lifecycle == "unloaded" || t.lifecycle == "hibernated") {
    m.chips.push_back("Asleep");
  }
  const std::string site =
      gurl.SchemeIsHTTPOrHTTPS() ? PreviewSite(host) : std::string();
  // Gmail's card is drawn from the account's unread mail, never a page: the feed's count and newest
  // messages, else the count in the tab's title. Without a feed answer a title with no count says nothing about the
  // mail (Gmail's title carries the inbox count only on list views, never on a message, a label, a search or while
  // loading), so the card is the plain one: it never claims zero unless the feed said zero.
  if (site == "Gmail") {
    const int title_unread = t.mail ? 0 : UnreadCount(t.title);
    if (!t.mail && title_unread == 0) {
      m.width = kHoverMaxPill + 2 * kHoverMargin;
      m.height = kHoverPill + 2 * kHoverMargin;
      return m;
    }
    m.kind = "mail";
    m.site = site;
    m.chips.clear();
    if (t.mail) {
      m.mail_source = "feed";
      m.unread = t.mail->count;
      if (m.unread > 0) {
        m.mail_items = t.mail->items;
      }
    } else {
      m.mail_source = "title";
      m.unread = title_unread;
    }
    m.width = kMailCardWidth + 2 * kHoverMargin;
    m.height = MailCardHeight(m.mail_items.size()) + 2 * kHoverMargin;
    return m;
  }
  // A preview site gets the live preview whether its tab is loaded or asleep (Hover lays the compact page,
  // its own page, over it): a restored favorite or pinned row is asleep until opened.
  if (!site.empty()) {
    m.kind = "preview";
    m.site = site;
    m.chips.clear();
    m.width = kLivePreviewWidth + 2 * kHoverMargin;
    m.height = kLivePreviewHeight + 2 * kHoverMargin;
    return m;
  }
  m.width = kHoverMaxPill + 2 * kHoverMargin;
  m.height = kHoverPill + 2 * kHoverMargin;
  return m;
}

gfx::Rect HoverCardRect(const gfx::Size& content,
                        const gfx::Rect& anchor,
                        const HoverCardModel& model) {
  const int x = std::clamp(anchor.x() + kHoverOffsetX - kHoverMargin, 0,
                           std::max(0, content.width() - model.width));
  const int y = std::clamp(anchor.y() + kHoverOffsetY - kHoverMargin, 0,
                           std::max(0, content.height() - model.height));
  return gfx::Rect(x, y, model.width, model.height);
}

std::optional<gfx::Rect> PassiveTopOverlayRect(
    const gfx::Size& content,
    const std::vector<OverlaySurfaceSnapshot>& surfaces) {
  if (surfaces.empty()) {
    return std::nullopt;
  }
  gfx::Rect covered;
  for (const OverlaySurfaceSnapshot& surface : surfaces) {
    if (surface.modality != "passive" || !surface.rect ||
        surface.rect->IsEmpty()) {
      return std::nullopt;
    }
    covered.Union(*surface.rect);
  }
  covered.Intersect(gfx::Rect(content));
  if (covered.IsEmpty()) {
    return std::nullopt;
  }
  return covered;
}

}  // namespace clu
