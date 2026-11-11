// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_copy_url.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

// The cases of stripTracking in src/shared/url.test.ts and normalize.test.ts.
TEST(CluCopyUrlTest, StripTrackingKeepsOrderEncodingAndFragment) {
  EXPECT_EQ(StripTracking(GURL("https://a.com/?utm_source=x&id=2#h")),
            "https://a.com/?id=2#h");
  EXPECT_EQ(StripTracking(GURL("https://a.com/?utm_source=x&id=2&fbclid=y#h")),
            "https://a.com/?id=2#h");
  EXPECT_EQ(StripTracking(GURL("https://a.com/p?utm_medium=a&gclid=b")),
            "https://a.com/p");
  EXPECT_EQ(StripTracking(GURL("https://a.com/?q=a+b%20c&mc_cid=1")),
            "https://a.com/?q=a+b%20c");
  // Every key of the list, any case, and an escaped key.
  EXPECT_EQ(StripTracking(GURL("https://a.com/?UTM_Campaign=1&GCLSRC=2&dclid=3"
                               "&msclkid=4&mc_eid=5&igshid=6&yclid=7&twclid=8"
                               "&_hsenc=9&_hsmi=10&ref_src=11&%75tm_x=12&k=v")),
            "https://a.com/?k=v");
  // Empty pairs go, a key without a value stays.
  EXPECT_EQ(StripTracking(GURL("https://a.com/?&a&&b=1&")),
            "https://a.com/?a&b=1");
  // No query: the spec as it is.
  EXPECT_EQ(StripTracking(GURL("https://a.com/x#y")), "https://a.com/x#y");
  // utm is only a prefix match on the key, never on the value.
  EXPECT_EQ(StripTracking(GURL("https://a.com/?id=utm_x")),
            "https://a.com/?id=utm_x");
  // Junk never throws.
  EXPECT_EQ(StripTracking(GURL("not a url")), "");
}

TEST(CluCopyUrlTest, OnlyWebPagesAreShareable) {
  EXPECT_EQ(ShareableUrl(GURL("https://a.com/?utm_source=x&id=1")),
            "https://a.com/?id=1");
  EXPECT_EQ(ShareableUrl(GURL("http://localhost:8080/a")),
            "http://localhost:8080/a");
  EXPECT_EQ(ShareableUrl(GURL("about:blank")), std::nullopt);
  EXPECT_EQ(ShareableUrl(GURL("chrome://settings")), std::nullopt);
  EXPECT_EQ(ShareableUrl(GURL("chrome://clu-frame/")), std::nullopt);
  EXPECT_EQ(ShareableUrl(GURL("file:///etc/passwd")), std::nullopt);
  EXPECT_EQ(ShareableUrl(GURL("data:text/html,x")), std::nullopt);
  EXPECT_EQ(ShareableUrl(GURL("javascript:alert(1)")), std::nullopt);
  EXPECT_EQ(ShareableUrl(GURL()), std::nullopt);
}

TEST(CluCopyUrlTest, MarkdownLinkEscapesAndFallsBack) {
  EXPECT_EQ(MarkdownLink(u"a [b] \\ c", "https://x.test/a_(b)"),
            "[a \\[b\\] \\\\ c](https://x.test/a_%28b%29)");
  EXPECT_EQ(MarkdownLink(u" two\n lines\t", "https://x.test/"),
            "[two lines](https://x.test/)");
  EXPECT_EQ(MarkdownLink(u"", "https://x.test/[1]"),
            "[https://x.test/\\[1\\]](https://x.test/[1])");
  EXPECT_EQ(MarkdownLink(std::u16string(1000, u't'), "https://x.test/").size(),
            kMaxLinkTitle + std::string("[](https://x.test/)").size());
  // Whitespace in the target is encoded too, and a cut never splits a surrogate pair.
  EXPECT_EQ(MarkdownLink(u"x", "https://x.test/a b"),
            "[x](https://x.test/a%20b)");
  std::u16string emoji(kMaxLinkTitle - 1, u'a');
  emoji += u"\U0001F600";
  const std::string cut = MarkdownLink(emoji, "https://x.test/");
  EXPECT_EQ(cut, "[" + std::string(kMaxLinkTitle - 1, 'a') +
                     "](https://x.test/)");
}

}  // namespace
}  // namespace clu
