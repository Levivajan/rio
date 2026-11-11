// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_key.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

TEST(CluSiteKeyTest, LowercaseHostWithOneLeadingWwwRemoved) {
  EXPECT_EQ(SiteKey(GURL("https://www.A.com/x")), "a.com");
  EXPECT_EQ(SiteKey(GURL("http://127.0.0.1:8080")), "127.0.0.1");
  EXPECT_EQ(SiteKey(GURL("https://www.www.a.com/")), "www.a.com");
  EXPECT_EQ(SiteKey(GURL("http://localhost/")), "localhost");
}

TEST(CluSiteKeyTest, NoSiteForAnythingButHttpAndHttps) {
  EXPECT_EQ(SiteKey(GURL("about:blank")), std::nullopt);
  EXPECT_EQ(SiteKey(GURL("chrome://clu-frame/")), std::nullopt);
  EXPECT_EQ(SiteKey(GURL("chrome-extension://abc/popup.html")), std::nullopt);
  EXPECT_EQ(SiteKey(GURL("file:///etc/hosts")), std::nullopt);
  EXPECT_EQ(SiteKey(GURL("not a url")), std::nullopt);
  EXPECT_EQ(SiteKey(GURL("https://a.com/" + std::string(40 * 1024, 'x'))),
            std::nullopt);
}

TEST(CluSiteKeyTest, OnlyKeysSurviveTheRoundTrip) {
  EXPECT_TRUE(IsSiteKey("a.com"));
  EXPECT_TRUE(IsSiteKey("127.0.0.1"));
  EXPECT_FALSE(IsSiteKey("www.a.com"));
  EXPECT_FALSE(IsSiteKey("A.com"));
  EXPECT_FALSE(IsSiteKey("a.com/path"));
  EXPECT_FALSE(IsSiteKey(""));
  EXPECT_FALSE(IsSiteKey("a com"));
}

TEST(CluSiteKeyTest, HostMatchesItsDomainAndParents) {
  EXPECT_TRUE(HostMatchesDomain("a.com", "a.com"));
  EXPECT_TRUE(HostMatchesDomain("news.a.com", "a.com"));
  EXPECT_FALSE(HostMatchesDomain("bada.com", "a.com"));
  EXPECT_FALSE(HostMatchesDomain("a.com", "news.a.com"));
  EXPECT_FALSE(HostMatchesDomain("a.com", ""));
}

TEST(CluSiteKeyTest, LocalSitesStartInDeveloperMode) {
  EXPECT_TRUE(DevModeDefault(*SiteKey(GURL("http://localhost:3000/"))));
  EXPECT_TRUE(DevModeDefault(*SiteKey(GURL("http://127.0.0.1:8080/a"))));
  EXPECT_TRUE(DevModeDefault(*SiteKey(GURL("http://app.localhost/"))));
  EXPECT_TRUE(DevModeDefault(*SiteKey(GURL("http://[::1]:5173/"))));
  EXPECT_FALSE(DevModeDefault(*SiteKey(GURL("https://example.com/"))));
  EXPECT_FALSE(DevModeDefault(*SiteKey(GURL("https://localhost.example.com/"))));
}

}  // namespace
}  // namespace clu
