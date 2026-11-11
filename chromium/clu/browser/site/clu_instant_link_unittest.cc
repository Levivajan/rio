// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_instant_link.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

constexpr char kLucky[] =
    "https://www.google.com/search?q=python+language+official+site&btnI=1";

TEST(CluInstantLinkTest, GoogleRedirectNoticeGivesTheTopResult) {
  // What Google answers.
  EXPECT_EQ(InstantLinkTarget(
                GURL(kLucky), GURL("https://www.google.com/url?q=https://www.python.org/")),
            GURL("https://www.python.org/"));
  // Escaped, with Google's extra fields, on a country domain.
  EXPECT_EQ(InstantLinkTarget(
                GURL("https://www.google.de/search?btnI=1&q=a"),
                GURL("https://www.google.de/url?sa=t&q=https%3A%2F%2Fa.test%2Fx%3Fy%3D1&usg=z")),
            GURL("https://a.test/x?y=1"));
  EXPECT_EQ(InstantLinkTarget(GURL(kLucky),
                              GURL("https://google.com/url?url=http://b.test/")),
            GURL("http://b.test/"));
  EXPECT_EQ(InstantLinkTarget(
                GURL("https://www.google.co.uk/search?q=a&btnI=1"),
                GURL("https://www.google.co.uk/url?q=https://c.test/")),
            GURL("https://c.test/"));
}

TEST(CluInstantLinkTest, EverythingElseRedirectsAsUsual) {
  const GURL notice("https://www.google.com/url?q=https://www.python.org/");
  // Not an Instant Links search.
  EXPECT_FALSE(InstantLinkTarget(
      GURL("https://www.google.com/search?q=python"), notice));
  EXPECT_FALSE(InstantLinkTarget(
      GURL("https://www.google.com/search?q=python&btnI="), notice));
  EXPECT_FALSE(InstantLinkTarget(
      GURL("http://www.google.com/search?q=python&btnI=1"), notice));
  EXPECT_FALSE(InstantLinkTarget(
      GURL("https://evil.test/search?q=python&btnI=1"), notice));
  EXPECT_FALSE(InstantLinkTarget(
      GURL("https://www.google.evil.test/search?btnI=1"), notice));
  // Not Google's /url page, or nothing to follow.
  EXPECT_FALSE(InstantLinkTarget(GURL(kLucky), GURL("https://www.python.org/")));
  EXPECT_FALSE(InstantLinkTarget(
      GURL(kLucky), GURL("https://evil.test/url?q=https://www.python.org/")));
  EXPECT_FALSE(InstantLinkTarget(GURL(kLucky), GURL("https://www.google.com/url")));
  EXPECT_FALSE(InstantLinkTarget(
      GURL(kLucky), GURL("https://www.google.com/search?q=python")));
  // Only http and https targets with a host.
  EXPECT_FALSE(InstantLinkTarget(
      GURL(kLucky), GURL("https://www.google.com/url?q=javascript:alert(1)")));
  EXPECT_FALSE(InstantLinkTarget(
      GURL(kLucky), GURL("https://www.google.com/url?q=file:///etc/hosts")));
  EXPECT_FALSE(InstantLinkTarget(
      GURL(kLucky), GURL("https://www.google.com/url?q=not%20a%20url")));
  EXPECT_FALSE(InstantLinkTarget(
      GURL(kLucky), GURL("https://www.google.com/url?q=https://" +
                   std::string(9000, 'a') + ".test/")));
  EXPECT_FALSE(InstantLinkTarget(GURL(), GURL()));
}

}  // namespace
}  // namespace clu
