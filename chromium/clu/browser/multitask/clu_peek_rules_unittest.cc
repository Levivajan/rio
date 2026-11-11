// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_peek_rules.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The shouldPeek table of docs/plan/05-multitask.md T4 on Chromium's inputs,
// decision 50's host rule and the card bounds of 05 "Bounds".
namespace clu {
namespace {

PeekInput Click(PeekSource source, const char* from, const char* to) {
  PeekInput input;
  input.source = source;
  input.source_url = GURL(from);
  input.target_url = GURL(to);
  input.via = PeekVia::kNavigate;
  input.disposition = PeekDisposition::kCurrentTab;
  input.link_click = true;
  return input;
}

TEST(CluPeekRulesTest, HostsCompareAfterDroppingOneLeadingWww) {
  EXPECT_EQ(PeekHost(GURL("https://www.Example.com/a")), "example.com");
  EXPECT_EQ(PeekHost(GURL("https://www.www.example.com/")), "www.example.com");
  EXPECT_EQ(PeekHost(GURL("file:///tmp/a.html")), "");
  EXPECT_FALSE(HostsDiffer(GURL("https://www.example.com/a"),
                           GURL("https://example.com/b")));
  EXPECT_TRUE(HostsDiffer(GURL("https://mail.example.com/"),
                          GURL("https://docs.example.com/")));
  // The fixture server's pair is cross-site.
  EXPECT_TRUE(HostsDiffer(GURL("http://127.0.0.1:8000/links.html"),
                          GURL("http://localhost:8000/target.html")));
  EXPECT_FALSE(HostsDiffer(GURL("http://127.0.0.1:8000/a"),
                           GURL("http://127.0.0.1:9000/b")));
}

TEST(CluPeekRulesTest, ThePeekTable) {
  constexpr char kPinned[] = "http://127.0.0.1/links.html";
  constexpr char kOther[] = "http://localhost/target.html";
  constexpr char kSame[] = "http://127.0.0.1/target.html";

  // Pinned tab, cross-site link click: Peek.
  EXPECT_EQ(ShouldPeek(Click(PeekSource::kPinned, kPinned, kOther)),
            PeekDecision::kPeek);
  EXPECT_EQ(ShouldPeek(Click(PeekSource::kFavorite, kPinned, kOther)),
            PeekDecision::kPeek);
  // Pinned tab, same-site link click: navigate.
  EXPECT_EQ(ShouldPeek(Click(PeekSource::kPinned, kPinned, kSame)),
            PeekDecision::kChromium);
  // A normal tab's plain click navigates.
  EXPECT_EQ(ShouldPeek(Click(PeekSource::kNormal, kPinned, kOther)),
            PeekDecision::kChromium);

  // Pinned tab, cross-site form POST or GET form: navigate.
  PeekInput post = Click(PeekSource::kPinned, kPinned, kOther);
  post.post = true;
  EXPECT_EQ(ShouldPeek(post), PeekDecision::kChromium);
  PeekInput form = Click(PeekSource::kPinned, kPinned, kOther);
  form.form_submission = true;
  EXPECT_EQ(ShouldPeek(form), PeekDecision::kChromium);
  // A script redirect is no link click.
  PeekInput script = Click(PeekSource::kPinned, kPinned, kOther);
  script.link_click = false;
  EXPECT_EQ(ShouldPeek(script), PeekDecision::kChromium);

  // Shift-click in any tab: Peek, even to the same host.
  PeekInput shift = Click(PeekSource::kNormal, kPinned, kSame);
  shift.via = PeekVia::kOpenUrl;
  shift.disposition = PeekDisposition::kNewWindow;
  EXPECT_EQ(ShouldPeek(shift), PeekDecision::kPeek);
  // Cmd-click and Cmd+Shift-click keep their tabs.
  PeekInput cmd = shift;
  cmd.disposition = PeekDisposition::kBackgroundTab;
  EXPECT_EQ(ShouldPeek(cmd), PeekDecision::kChromium);
  cmd.disposition = PeekDisposition::kForegroundTab;
  EXPECT_EQ(ShouldPeek(cmd), PeekDecision::kChromium);
  // The context menu's Open Link in New Window (browser-initiated, with a
  // gesture) opens a window, from any tab and to any host.
  PeekInput menu = shift;
  menu.from_browser = true;
  EXPECT_EQ(ShouldPeek(menu), PeekDecision::kChromium);
  menu.source = PeekSource::kPinned;
  menu.target_url = GURL(kOther);
  EXPECT_EQ(ShouldPeek(menu), PeekDecision::kChromium);

  // target=_blank from a pinned tab to another host: Peek; to the same host or
  // from a normal tab: a tab.
  PeekInput blank = Click(PeekSource::kPinned, kPinned, kOther);
  blank.via = PeekVia::kNewContents;
  blank.disposition = PeekDisposition::kForegroundTab;
  EXPECT_EQ(ShouldPeek(blank), PeekDecision::kPeek);
  blank.target_url = GURL(kSame);
  EXPECT_EQ(ShouldPeek(blank), PeekDecision::kChromium);
  blank.target_url = GURL(kOther);
  blank.source = PeekSource::kNormal;
  EXPECT_EQ(ShouldPeek(blank), PeekDecision::kChromium);

  // A sized popup stays a popup.
  PeekInput sized = Click(PeekSource::kPinned, kPinned, kOther);
  sized.via = PeekVia::kNewContents;
  sized.disposition = PeekDisposition::kNewPopup;
  EXPECT_EQ(ShouldPeek(sized), PeekDecision::kChromium);
  sized.disposition = PeekDisposition::kNewWindow;
  sized.sized = true;
  EXPECT_EQ(ShouldPeek(sized), PeekDecision::kChromium);

  // javascript: and file: targets are never peeked.
  EXPECT_EQ(ShouldPeek(Click(PeekSource::kPinned, kPinned, "javascript:1")),
            PeekDecision::kChromium);
  PeekInput file = shift;
  file.target_url = GURL("file:///etc/hosts");
  EXPECT_EQ(ShouldPeek(file), PeekDecision::kChromium);
}

// Settings, Links (docs/chromium/parity-settings.md): each switch off hands its links back to Chromium, and leaves
// the other kind alone.
TEST(CluPeekRulesTest, TheLinksSwitchesTurnEachKindOff) {
  constexpr char kPinned[] = "http://127.0.0.1/links.html";
  constexpr char kOther[] = "http://localhost/target.html";
  PeekInput click = Click(PeekSource::kPinned, kPinned, kOther);
  PeekInput shift = Click(PeekSource::kNormal, kPinned, kOther);
  shift.via = PeekVia::kOpenUrl;
  shift.disposition = PeekDisposition::kNewWindow;
  PeekInput blank = Click(PeekSource::kPinned, kPinned, kOther);
  blank.via = PeekVia::kNewContents;
  blank.disposition = PeekDisposition::kForegroundTab;

  click.peek_other_sites = false;
  blank.peek_other_sites = false;
  shift.peek_other_sites = false;
  EXPECT_EQ(ShouldPeek(click), PeekDecision::kChromium);
  EXPECT_EQ(ShouldPeek(blank), PeekDecision::kChromium);
  EXPECT_EQ(ShouldPeek(shift), PeekDecision::kPeek);

  click.peek_other_sites = blank.peek_other_sites = shift.peek_other_sites =
      true;
  shift.peek_on_shift_click = false;
  click.peek_on_shift_click = false;
  EXPECT_EQ(ShouldPeek(shift), PeekDecision::kChromium);
  EXPECT_EQ(ShouldPeek(click), PeekDecision::kPeek);
  PeekInput page_window = blank;
  page_window.disposition = PeekDisposition::kNewWindow;
  page_window.peek_on_shift_click = false;
  EXPECT_EQ(ShouldPeek(page_window), PeekDecision::kChromium);
}

TEST(CluPeekRulesTest, TheCardIsCenteredInThePageRect) {
  // 80.8% of 1000 is 808, under the 1100 cap. The card is 12 from the window's top and bottom: 2 inside a page card
  // inset 10, the whole page height for a page further down.
  EXPECT_EQ(PeekCardRect(gfx::Rect(200, 10, 1000, 780)),
            gfx::Rect(296, 12, 808, 776));
  EXPECT_EQ(PeekCardRect(gfx::Rect(200, 50, 1000, 700)),
            gfx::Rect(296, 50, 808, 700));
  // At a 1280 by 800 window (sidebar 213): 853 by 776.
  EXPECT_EQ(PeekCardRect(gfx::Rect(214, 10, 1056, 780)).size(),
            gfx::Size(853, 776));
  EXPECT_EQ(PeekCardRect(gfx::Rect(0, 0, 1000, 800)).y(), 12);
  // Wide pages cap at 1100.
  EXPECT_EQ(PeekCardRect(gfx::Rect(0, 0, 2000, 1000)).width(), 1100);
  // Tiny pages keep 360 by 240.
  const gfx::Rect tiny = PeekCardRect(gfx::Rect(0, 0, 200, 100));
  EXPECT_EQ(tiny.width(), 360);
  EXPECT_EQ(tiny.height(), 240);
}

}  // namespace
}  // namespace clu
