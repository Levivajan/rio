// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_url_policy.h"

#include <string>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

TEST(CluUrlPolicyTest, TypedUrlsFollowChromiumsRules) {
  EXPECT_TRUE(IsTypedUrlAllowed(GURL("https://a.test/path?q=1")));
  EXPECT_TRUE(IsTypedUrlAllowed(GURL("http://localhost:8080/")));
  EXPECT_TRUE(IsTypedUrlAllowed(GURL("file:///tmp/a.html")));
  EXPECT_TRUE(IsTypedUrlAllowed(GURL("chrome://settings/")));
  EXPECT_TRUE(IsTypedUrlAllowed(GURL("about:blank")));
  EXPECT_TRUE(IsTypedUrlAllowed(GURL("view-source:https://a.test/")));

  EXPECT_FALSE(IsTypedUrlAllowed(GURL()));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("not a url")));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("javascript:alert(1)")));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("data:text/html,<p>x</p>")));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("blob:https://a.test/0f1e")));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("view-source:javascript:alert(1)")));
  EXPECT_FALSE(
      IsTypedUrlAllowed(GURL("view-source:view-source:https://a.test/")));
  // Clu's own frame and overlay never load in a tab.
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("chrome://clu-frame/")));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("chrome://clu-overlay/?layer=top")));
  EXPECT_FALSE(IsTypedUrlAllowed(GURL("view-source:chrome://clu-frame/")));
  EXPECT_FALSE(IsTypedUrlAllowed(
      GURL("https://a.test/" + std::string(40 * 1024, 'p'))));
}

TEST(CluUrlPolicyTest, HistoryRowsAreWebPagesOnly) {
  EXPECT_TRUE(IsWebUrl(GURL("https://a.test/")));
  EXPECT_FALSE(IsWebUrl(GURL("chrome://settings/")));
  EXPECT_FALSE(IsWebUrl(GURL("file:///tmp/a.html")));
  EXPECT_FALSE(IsWebUrl(GURL("javascript:alert(1)")));
}

TEST(CluUrlPolicyTest, FaviconsComeFromFavicon2) {
  EXPECT_EQ(FaviconUrlFor(GURL("https://a.test/x?y=1")),
            "chrome://favicon2/?size=16&scaleFactor=2x&pageUrl="
            "https%3A%2F%2Fa.test%2Fx%3Fy%3D1&allowGoogleServerFallback=0");
  EXPECT_EQ(FaviconUrlFor(GURL("https://a.test/"), "12 3"),
            "chrome://favicon2/?size=16&scaleFactor=2x&pageUrl="
            "https%3A%2F%2Fa.test%2F&allowGoogleServerFallback=0&cluIcon=12%203");
  // Chromium's settings row takes its own icon (the gear) from favicon2.
  EXPECT_EQ(FaviconUrlFor(GURL("chrome://settings/autofill")),
            "chrome://favicon2/?size=16&scaleFactor=2x&pageUrl="
            "chrome%3A%2F%2Fsettings%2Fautofill&allowGoogleServerFallback=0");
  EXPECT_FALSE(FaviconUrlFor(GURL("about:blank")));
  EXPECT_FALSE(FaviconUrlFor(GURL()));
  EXPECT_FALSE(
      FaviconUrlFor(GURL("https://a.test/" + std::string(3000, 'p'))));
}

TEST(CluUrlPolicyTest, HugeUrlsShowOnlyTheirScheme) {
  EXPECT_EQ(StateUrl(GURL("https://a.test/")), "https://a.test/");
  EXPECT_EQ(StateUrl(GURL("data:text/plain," + std::string(40 * 1024, 'd'))),
            "data:");
}

// M8: the rio:// display alias.
TEST(CluUrlPolicyTest, CluPagesShowAsCluUrlsAndMapBack) {
  // docs/chromium/parity-settings.md section 5: rio://settings is Chromium's settings, shown like any row.
  EXPECT_EQ(DisplayUrl("chrome://settings/"), "rio://settings/");
  EXPECT_EQ(DisplayUrl("chrome://settings/autofill"), "rio://settings/autofill");
  EXPECT_EQ(DisplayUrl("chrome://settings/help"), "rio://settings/help");
  // Rio's own settings page lives in the settings window and never shows as an address.
  EXPECT_EQ(DisplayUrl("chrome://clu-settings/"), "chrome://clu-settings/");
  EXPECT_EQ(DisplayUrl("chrome://history/?q=harbor"), "rio://history/?q=harbor");
  EXPECT_EQ(DisplayUrl("chrome://extensions/"), "rio://extensions/");
  // Chromium's other pages and every web page stay as they are.
  EXPECT_EQ(DisplayUrl("chrome://history-sync/"), "chrome://history-sync/");
  EXPECT_EQ(DisplayUrl("https://clu-settings.example/"), "https://clu-settings.example/");
  EXPECT_EQ(StateUrl(GURL("chrome://history/")), "rio://history/");

  EXPECT_EQ(ResolveCluUrl(GURL("rio://settings/")), GURL("chrome://settings/"));
  EXPECT_EQ(ResolveCluUrl(GURL("rio://settings/privacy")),
            GURL("chrome://settings/privacy"));
  EXPECT_EQ(ResolveCluUrl(GURL("rio://history")), GURL("chrome://history/"));
  EXPECT_EQ(ResolveCluUrl(GURL("rio://HISTORY/")), GURL("chrome://history/"));
  EXPECT_EQ(ResolveCluUrl(GURL("rio://about/")), GURL("chrome://settings/help"));
  EXPECT_EQ(ResolveCluUrl(GURL("https://a.test/")), GURL("https://a.test/"));
  // Unknown aliases and Clu's own surfaces stay unloadable.
  for (const char* blocked : {"rio://frame/", "rio://overlay/", "rio://nope/", "rio:settings",
                              // Rio's settings page shows only in its window.
                              "chrome://clu-settings/",
                              // The old code-name scheme is no alias.
                              "clu://settings/"}) {
    EXPECT_FALSE(IsTypedUrlAllowed(ResolveCluUrl(GURL(blocked)))) << blocked;
  }
  EXPECT_TRUE(IsTypedUrlAllowed(ResolveCluUrl(GURL("rio://settings/"))));
  EXPECT_TRUE(IsTypedUrlAllowed(ResolveCluUrl(GURL("rio://history/"))));
  // The round trip.
  EXPECT_EQ(DisplayUrl(ResolveCluUrl(GURL("rio://extensions/")).spec()), "rio://extensions/");
  EXPECT_EQ(DisplayUrl(ResolveCluUrl(GURL("rio://settings/")).spec()), "rio://settings/");
}

// A never-loaded row's icon comes from the page's declared icons, best first, and junk markup never crashes.
TEST(CluUrlPolicyTest, DeclaredIconUrlsPicksTheLargestSuitableIcon) {
  const GURL base("https://app.example.test/home");
  const std::string html =
      "<html><HEAD><title>Home</title>"
      "<link rel=stylesheet href=/a.css>"
      "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/i.svg\">"
      "<link rel='mask-icon' href='/mask.png'>"
      "<LINK REL=\"shortcut icon\" href=\"/favicon-16.png\">"
      "<link rel=\"icon\" sizes=\"32x32 48x48\" href=\"static/i48.png\" />"
      "<link rel=icon sizes=1024x1024 href=//cdn.example.test/huge.png>"
      "<link rel=\"apple-touch-icon\" href=\"/touch.png\">"
      "<link rel=\"icon\" href=\"javascript:alert(1)\">"
      "<link rel=\"icon\" href=\"  \">"
      "<link rel=\"icon\" href=\"/favicon-16.png\">"
      "</head><body><link rel=icon href=/body.png></body></html>";
  EXPECT_EQ(DeclaredIconUrls(html, base),
            (std::vector<GURL>{GURL("https://app.example.test/touch.png"),
                               GURL("https://app.example.test/static/i48.png"),
                               GURL("https://app.example.test/favicon-16.png"),
                               GURL("https://cdn.example.test/huge.png")}));
  // Junk: no head end, an unterminated tag or quote, binary bytes, a non-web base.
  EXPECT_TRUE(DeclaredIconUrls("", base).empty());
  EXPECT_TRUE(DeclaredIconUrls("<link rel=icon href=\"/a.png", base).empty());
  EXPECT_TRUE(DeclaredIconUrls(std::string("<link\0rel=icon\xff\xfe>", 17), base).empty());
  EXPECT_TRUE(DeclaredIconUrls("<link rel=icon href=/a.png>",
                               GURL("chrome://settings/"))
                  .empty());
  EXPECT_TRUE(DeclaredIconUrls("<link rel=icon href=/a.png>", GURL()).empty());
  EXPECT_EQ(DeclaredIconUrls("<link rel=icon sizes=junk href=/a.png>", base),
            std::vector<GURL>{GURL("https://app.example.test/a.png")});
  EXPECT_EQ(DeclaredIconUrls("<link = rel=icon href='/b.png' x>", base),
            std::vector<GURL>{GURL("https://app.example.test/b.png")});
  // A huge page: only the first 512 KB is read, and at most 16 icons.
  std::string huge(600 * 1024, 'a');
  huge += "<link rel=icon href=/late.png>";
  EXPECT_TRUE(DeclaredIconUrls(huge, base).empty());
  std::string many;
  for (int i = 0; i < 100; ++i) {
    many += "<link rel=icon href=/i" + base::NumberToString(i) + ".png>";
  }
  EXPECT_EQ(DeclaredIconUrls(many, base).size(), 16u);
}

}  // namespace
}  // namespace clu
