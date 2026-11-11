// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_overlay_rects.h"

#include <optional>
#include <string>

#include "clu/browser/ui/webui/clu_state_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

// The cases of src/shared/tabs.test.ts (hover card model, middle truncation, chips), and the switcher and toast
// rects.
namespace clu {
namespace {

TEST(CluOverlayRectsTest, ToastSitsTopRightOneAtATime) {
  // The default card (sidebar 214, 10 inset): the toast 18 below the window's top and 16 in from its right.
  const gfx::Rect card(214, 10, 1280 - 214 - 10, 800 - 20);
  EXPECT_EQ(ToastRect(card, 1), gfx::Rect(952, 10, 320, 60));
  EXPECT_EQ(ToastRect(card, 9).height(), 44 + 16);
}

// Every toast is placed from the page card, so its 44 point card stays inside the page card wherever it is.
TEST(CluOverlayRectsTest, ToastStaysInsideThePageCard) {
  for (const gfx::Rect& card :
       {gfx::Rect(214, 10, 1056, 780), gfx::Rect(214, 50, 1056, 740),
        gfx::Rect(4, 0, 1276, 800), gfx::Rect(0, 0, 1280, 800),
        gfx::Rect(214, 10, 200, 300)}) {
    const gfx::Rect view = ToastRect(card, 1);
    gfx::Rect toast = view;
    toast.Inset(kToastPad);
    EXPECT_TRUE(card.Contains(toast)) << card.ToString();
    EXPECT_EQ(toast.y(), card.y() + kToastInsetTop) << card.ToString();
    EXPECT_EQ(toast.right(), card.right() - kToastInsetRight)
        << card.ToString();
    EXPECT_GE(view.y(), 0);
  }
}

TEST(CluOverlayRectsTest, SwitcherIsCenteredAndShrinksToFit) {
  const gfx::Rect five = SwitcherRect(gfx::Size(1280, 800), 5);
  EXPECT_EQ(five.width(), 5 * 176 + 4 * 10 + 40);
  EXPECT_EQ(five.height(), 170);
  EXPECT_EQ(five.x(), (1280 - five.width()) / 2);
  EXPECT_EQ(SwitcherRect(gfx::Size(400, 100), 5), gfx::Rect(16, 0, 368, 100));
}

TEST(CluOverlayRectsTest, MiddleTruncateKeepsBothEnds) {
  EXPECT_EQ(MiddleTruncate("abcdefghij", 5), "ab…ij");
  EXPECT_EQ(MiddleTruncate("short", 10), "short");
  EXPECT_EQ(MiddleTruncate("abc", 1), "…");
}

TEST(CluOverlayRectsTest, HostAndPathDropsSchemeQueryAndWww) {
  EXPECT_EQ(HostAndPath("https://www.a.test/p/q?x=1#f"), "a.test/p/q");
  EXPECT_EQ(HostAndPath("https://a.test/"), "a.test");
  EXPECT_EQ(HostAndPath("not a url"), "not a url");
  // Rio's own pages read by host, as the URL pill shows them.
  EXPECT_EQ(HostAndPath("rio://settings/"), "rio://settings");
  EXPECT_EQ(HostAndPath("rio://history/?q=a"), "rio://history");
  EXPECT_EQ(HostAndPath("rio://about"), "rio://about");
}

TEST(CluOverlayRectsTest, HoverCardChipsAndHeight) {
  HoverCardInput input;
  input.title = "Inbox";
  input.url = "https://mail.test/";
  input.muted = true;
  input.audible = true;
  input.lifecycle = "unloaded";
  const HoverCardModel m = BuildHoverCard(input);
  EXPECT_EQ(m.chips, (std::vector<std::string>{"Muted", "Asleep"}));
  EXPECT_EQ(m.title_lines, 1);
  EXPECT_EQ(m.width, 320 + 2 * 8);
  EXPECT_EQ(m.height, 28 + 2 * 8);

  input.title = std::string(40, 'x');
  input.muted = false;
  input.lifecycle = "live";
  input.off_pinned = true;
  input.pinned_url = "https://mail.test/home";
  const HoverCardModel two = BuildHoverCard(input);
  EXPECT_EQ(two.title_lines, 1);
  EXPECT_EQ(two.chips, std::vector<std::string>{"Playing"});
  EXPECT_EQ(two.pinned, "mail.test/home");
  EXPECT_EQ(two.height, m.height);
}

// Gmail's card is the mail card, loaded or asleep, never a live page. With the feed it shows the feed's count and its
// newest messages (at most 5) and grows a 44 tall row per message; with no unread mail it is the 64 tall two-line
// card.
TEST(CluOverlayRectsTest, GmailCardComesFromTheFeed) {
  HoverCardInput input;
  input.title = "Inbox (12) - me@example.com - Mail";
  input.url = "https://mail.google.com/mail/u/0/#inbox";
  input.audible = true;
  input.lifecycle = "unloaded";
  GmailUnread mail;
  mail.count = 40;
  for (int i = 0; i < 5; ++i) {
    mail.items.push_back({"Sender", "Subject", "2026-11-10T10:00:00Z",
                          "https://mail.google.com/mail?message_id=1"});
  }
  input.mail = mail;
  HoverCardModel m = BuildHoverCard(input);
  EXPECT_EQ(m.kind, "mail");
  EXPECT_EQ(m.site, "Gmail");
  EXPECT_EQ(m.mail_source, "feed");
  // The feed's count, not the stored title's.
  EXPECT_EQ(m.unread, 40);
  EXPECT_EQ(m.mail_items.size(), 5u);
  EXPECT_TRUE(m.chips.empty());
  EXPECT_EQ(m.width, 239 + 2 * 8);
  EXPECT_EQ(m.height, 6 + 30 + 5 * 44 + 6 + 2 * 8);
  input.mail->count = 3;
  input.mail->items.resize(3);
  EXPECT_EQ(BuildHoverCard(input).height, 6 + 30 + 3 * 44 + 6 + 2 * 8);
  // No unread mail: the two-line card, even though the old title says 12.
  input.mail = GmailUnread();
  m = BuildHoverCard(input);
  EXPECT_EQ(m.unread, 0);
  EXPECT_TRUE(m.mail_items.empty());
  EXPECT_EQ(m.height, 64 + 2 * 8);
  EXPECT_EQ(LivePreviewUrl(input.url), "");
}

// With the feed unavailable the card falls back to the title's count (the count-only card). A title with no
// count, live or asleep (a message view, a label, a search, a page still loading), gives the plain card, never
// "No unread emails" and never the Gmail page.
TEST(CluOverlayRectsTest, GmailCardFallsBackToTheTitle) {
  HoverCardInput input;
  input.title = "Inbox (37) - me@example.com - Mail";
  input.url = "https://mail.google.com/mail/u/1/#inbox";
  input.lifecycle = "live";
  HoverCardModel m = BuildHoverCard(input);
  EXPECT_EQ(m.kind, "mail");
  EXPECT_EQ(m.mail_source, "title");
  EXPECT_EQ(m.unread, 37);
  EXPECT_EQ(m.height, 64 + 2 * 8);
  for (const char* title : {"Inbox - me@example.com - Mail",
                            "Re: invoice - me@example.com - Gmail", "Gmail",
                            ""}) {
    input.title = title;
    for (const char* lifecycle : {"live", "unloaded"}) {
      input.lifecycle = lifecycle;
      m = BuildHoverCard(input);
      EXPECT_EQ(m.kind, "title") << title << " " << lifecycle;
      EXPECT_TRUE(m.site.empty());
      EXPECT_TRUE(m.mail_source.empty());
      EXPECT_EQ(m.height, 28 + 2 * 8);
    }
  }
  input.title = "Inbox (4) - Mail";
  EXPECT_EQ(BuildHoverCard(input).mail_source, "title");
  EXPECT_EQ(BuildHoverCard(input).unread, 4);
  // The feed's zero is the only zero.
  input.title = "Re: invoice - me@example.com - Gmail";
  input.mail = GmailUnread();
  m = BuildHoverCard(input);
  EXPECT_EQ(m.kind, "mail");
  EXPECT_EQ(m.mail_source, "feed");
  EXPECT_EQ(m.unread, 0);
}

// A live page on a preview site gets the 239 x 256 live card named for its site (previewSite() in
// src/shared/tabs.ts); other sites, other schemes and junk keep the pill.
TEST(CluOverlayRectsTest, PreviewSitesGetTheLiveCard) {
  HoverCardInput input;
  input.title = "Inbox (3) - Mail";
  input.url = "https://calendar.google.com/calendar/u/0/r/day";
  HoverCardModel m = BuildHoverCard(input);
  EXPECT_EQ(m.kind, "preview");
  EXPECT_EQ(m.width, 239 + 2 * 8);
  EXPECT_EQ(m.height, 256 + 2 * 8);
  EXPECT_EQ(BuildHoverCard(input).site, "Google Calendar");
  EXPECT_EQ(BuildHoverCard(input).unread, 0);
  input.url = "https://meet.google.com/abc-defg-hij";
  EXPECT_EQ(BuildHoverCard(input).site, "Google Meet");
  // Every other site keeps the title card.
  for (const char* url : {"https://www.notion.so/page", "https://github.com/x",
                          "https://outlook.office.com/mail/",
                          "https://example.com/", "ftp://github.com/",
                          "not a url", "https://calendar.google.com.evil.test/"}) {
    input.url = url;
    EXPECT_EQ(BuildHoverCard(input).kind, "title") << url;
  }
  // An asleep Calendar or Meet row (a restored pinned row) gets the live card as a loaded one does.
  input.lifecycle = "unloaded";
  input.url = "https://calendar.google.com/";
  EXPECT_EQ(BuildHoverCard(input).kind, "preview");
  input.url = "https://meet.google.com/";
  EXPECT_EQ(BuildHoverCard(input).kind, "preview");
  input.url = "https://example.com/";
  EXPECT_EQ(BuildHoverCard(input).kind, "title");
  EXPECT_EQ(PreviewSite("www.github.com"), "");
  EXPECT_EQ(PreviewSite(""), "");
}

// The card's page asks for the sites' phone views, a valid header value.
TEST(CluOverlayRectsTest, LivePreviewUserAgentIsAPhoneBrowser) {
  const std::string ua = LivePreviewUserAgent();
  EXPECT_NE(ua.find("iPhone"), std::string::npos);
  EXPECT_NE(ua.find("Mobile"), std::string::npos);
  EXPECT_EQ(ua.find_first_of("\r\n"), std::string::npos);
  EXPECT_EQ(ua.find('\0'), std::string::npos);
}

TEST(CluOverlayRectsTest, LivePreviewPageFillsTheCardInsideItsShadow) {
  EXPECT_EQ(LivePreviewPageRect(gfx::Rect(300, 100, 255, 272)),
            gfx::Rect(308, 108, 239, 256));
}

TEST(CluOverlayRectsTest, LivePreviewUrlIsTheSitesCompactPageOnTheTabsAccount) {
  EXPECT_EQ(LivePreviewUrl("https://calendar.google.com/calendar/u/1/r/week"),
            "https://calendar.google.com/calendar/u/1/r/day");
  EXPECT_EQ(LivePreviewUrl("https://calendar.google.com/"),
            "https://calendar.google.com/calendar/u/0/r/day");
  EXPECT_EQ(LivePreviewUrl("https://meet.google.com/abc-defg-hij"),
            "https://meet.google.com/landing?authuser=0");
  // A junk account index falls back to the first account.
  EXPECT_EQ(LivePreviewUrl("https://calendar.google.com/calendar/u/999x/"),
            "https://calendar.google.com/calendar/u/0/r/day");
  // Gmail has no live page: its card is the mail card.
  EXPECT_EQ(LivePreviewUrl("https://mail.google.com/mail/u/2/#sent"), "");
  for (const char* url :
       {"https://example.com/", "https://github.com/", "not a url", "",
        "ftp://mail.google.com/", "https://calendar.google.com.evil.test/"}) {
    EXPECT_EQ(LivePreviewUrl(url), "") << url;
  }
}

TEST(CluOverlayRectsTest, UnreadCountReadsTheTitleLikeTheFrame) {
  EXPECT_EQ(UnreadCount("(3) Inbox"), 3);
  EXPECT_EQ(UnreadCount("Inbox (99+)"), 99);
  EXPECT_EQ(UnreadCount("Inbox (0)"), 0);
  EXPECT_EQ(UnreadCount("f(3) x"), 0);
  EXPECT_EQ(UnreadCount("(3)x"), 0);
  EXPECT_EQ(UnreadCount("(1234567) x"), 0);
  EXPECT_EQ(UnreadCount("( ) (x) (12"), 0);
  EXPECT_EQ(UnreadCount(std::string(400, '(')), 0);
}

TEST(CluOverlayRectsTest, AnEmptyTitleFallsBackToTheHost) {
  HoverCardInput input;
  input.title = "  \x01 ";
  input.url = "https://www.a.test/x";
  EXPECT_EQ(BuildHoverCard(input).title, "a.test");
}

TEST(CluOverlayRectsTest, HoverCardRectHangsFromThePointerInsideTheWindow) {
  HoverCardModel m;
  m.width = 336;
  m.height = 44;
  EXPECT_EQ(HoverCardRect(gfx::Size(1280, 800), gfx::Rect(100, 400, 0, 0), m),
            gfx::Rect(103, 404, 336, 44));
  EXPECT_EQ(HoverCardRect(gfx::Size(1280, 800), gfx::Rect(1270, 795, 0, 0), m),
            gfx::Rect(944, 756, 336, 44));
}

// docs/chromium/fixes.md: the tweak editor's top overlay covers only the editor, so the page beside it takes clicks;
// anything else in the top layer covers the window.
TEST(CluOverlayRectsTest, ThePassiveTopOverlayCoversOnlyItsSurfaces) {
  const gfx::Size window(1280, 800);
  OverlaySurfaceSnapshot editor;
  editor.surface = "tweak-editor";
  editor.modality = "passive";
  editor.rect = gfx::Rect(890, 10, 380, 780);
  EXPECT_EQ(PassiveTopOverlayRect(window, {editor}), gfx::Rect(890, 10, 380, 780));
  // Kept inside the window.
  editor.rect = gfx::Rect(1100, -20, 380, 900);
  EXPECT_EQ(PassiveTopOverlayRect(window, {editor}), gfx::Rect(1100, 0, 180, 800));
  // Nothing open, an editor with no rect or an empty one, or one off the window: the whole window.
  EXPECT_EQ(PassiveTopOverlayRect(window, {}), std::nullopt);
  editor.rect = std::nullopt;
  EXPECT_EQ(PassiveTopOverlayRect(window, {editor}), std::nullopt);
  editor.rect = gfx::Rect(890, 10, 0, 780);
  EXPECT_EQ(PassiveTopOverlayRect(window, {editor}), std::nullopt);
  editor.rect = gfx::Rect(2000, 10, 380, 780);
  EXPECT_EQ(PassiveTopOverlayRect(window, {editor}), std::nullopt);
  // A menu, a dialog or a prompt beside it covers the window again.
  editor.rect = gfx::Rect(890, 10, 380, 780);
  OverlaySurfaceSnapshot menu;
  menu.surface = "site-controls";
  menu.modality = "light-dismiss";
  menu.rect = gfx::Rect(20, 40, 300, 400);
  EXPECT_EQ(PassiveTopOverlayRect(window, {editor, menu}), std::nullopt);
  OverlaySurfaceSnapshot dialog;
  dialog.surface = "confirm";
  EXPECT_EQ(PassiveTopOverlayRect(window, {dialog}), std::nullopt);
}

}  // namespace
}  // namespace clu
