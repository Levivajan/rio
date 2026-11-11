// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_live_feed.h"

#include <string>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace clu {
namespace {

// The data decoder's XML tree, written as JSON.
base::Value Xml(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  EXPECT_TRUE(value) << json;
  return value ? std::move(*value) : base::Value();
}

std::string Text(std::string_view tag, std::string_view text) {
  return base::StrCat({R"({"type":"element","tag":")", tag,
                       R"(","children":[{"type":"text","text":")", text,
                       R"("}]})"});
}

GURL Base() {
  return GURL("https://news.test/feed.xml");
}

TEST(CluLiveFeedTest, ReadsRssItems) {
  const base::Value rss = Xml(base::StrCat(
      {R"({"type":"element","tag":"rss","children":[{"type":"element","tag":"channel","children":[)",
       Text("title", "  News \\n Test "), ",",
       R"({"type":"element","tag":"item","children":[)", Text("title", "One"), ",",
       Text("link", "https://news.test/1"), "]},",
       // No link: a permalink guid.
       R"({"type":"element","tag":"item","children":[)", Text("title", "Two"), ",",
       R"({"type":"element","tag":"guid","children":[{"type":"text","text":"https://news.test/2"}]}]},)",
       // Junk: a script link, a duplicate, an item with nothing.
       R"({"type":"element","tag":"item","children":[)", Text("link", "javascript:alert(1)"), "]},",
       R"({"type":"element","tag":"item","children":[)", Text("link", "https://news.test/1"), "]},",
       R"({"type":"element","tag":"item"},)",
       // No title: the URL stands in. A relative link resolves against the feed.
       R"({"type":"element","tag":"item","children":[)", Text("link", "/3"), "]}",
       "]}]}"}));
  const std::optional<LiveFeed> feed = ParseLiveFeed(rss, Base());
  ASSERT_TRUE(feed);
  EXPECT_EQ(feed->title, "News Test");
  EXPECT_EQ(feed->items,
            (std::vector<LiveFeedItem>{{"https://news.test/1", "One"},
                                       {"https://news.test/2", "Two"},
                                       {"https://news.test/3", "https://news.test/3"}}));
}

TEST(CluLiveFeedTest, ReadsAtomEntriesAndKeepsTwenty) {
  std::string entries;
  for (int i = 0; i < 25; ++i) {
    entries += base::StrCat(
        {i ? "," : "", R"({"type":"element","tag":"entry","children":[)",
         Text("title", base::StrCat({"v", base::NumberToString(i)})), ",",
         R"({"type":"element","tag":"link","attributes":{"rel":"self","href":"https://x.test/self"}},)",
         R"({"type":"element","tag":"link","attributes":{"rel":"alternate","href":"https://github.com/o/r/releases/tag/v)",
         base::NumberToString(i), R"("}}]})"});
  }
  const base::Value atom = Xml(base::StrCat(
      {R"({"type":"element","tag":"feed","children":[)",
       Text("title", "Release notes from r"), ",", entries, "]}"}));
  const std::optional<LiveFeed> feed = ParseLiveFeed(atom, GURL());
  ASSERT_TRUE(feed);
  EXPECT_EQ(feed->title, "Release notes from r");
  ASSERT_EQ(feed->items.size(), kMaxLiveFolderItems);
  EXPECT_EQ(feed->items[0],
            (LiveFeedItem{"https://github.com/o/r/releases/tag/v0", "v0"}));
}

TEST(CluLiveFeedTest, ReadsRdfAndRefusesWhatIsNoFeed) {
  const base::Value rdf = Xml(base::StrCat(
      {R"({"type":"element","tag":"rdf:RDF","children":[{"type":"element","tag":"channel","children":[)",
       Text("title", "Old"), R"(]},{"type":"element","tag":"item","children":[)",
       Text("title", "A"), ",", Text("link", "http://old.test/a"), "]}]}"}));
  const std::optional<LiveFeed> feed = ParseLiveFeed(rdf, GURL());
  ASSERT_TRUE(feed);
  EXPECT_EQ(feed->items, (std::vector<LiveFeedItem>{{"http://old.test/a", "A"}}));

  EXPECT_FALSE(ParseLiveFeed(Xml(R"({"type":"element","tag":"html"})"), GURL()));
  EXPECT_FALSE(ParseLiveFeed(Xml(R"({"type":"element","tag":"rss"})"), GURL()));
  EXPECT_FALSE(ParseLiveFeed(base::Value("rss"), GURL()));
  EXPECT_FALSE(ParseLiveFeed(base::Value(), GURL()));
}

TEST(CluLiveFeedTest, SourcesNameTheirFeed) {
  EXPECT_EQ(LiveFolderFeedUrl(kLiveFeed, "news.test/feed.xml"),
            GURL("https://news.test/feed.xml"));
  EXPECT_EQ(LiveFolderFeedUrl(kLiveFeed, " http://news.test/rss#top "),
            GURL("http://news.test/rss"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveFeed, "file:///etc/hosts"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveFeed, "javascript:alert(1)"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveFeed, "https://user:pw@news.test/"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveFeed, "not a url"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveFeed, ""));

  EXPECT_EQ(LiveFolderFeedUrl(kLiveGithubReleases, "chromium/chromium"),
            GURL("https://github.com/chromium/chromium/releases.atom"));
  EXPECT_EQ(LiveFolderFeedUrl(kLiveGithubCommits,
                              "https://github.com/a-b/c.d.git/tree/main"),
            GURL("https://github.com/a-b/c.d/commits.atom"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveGithubReleases, "just-one"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveGithubReleases, "../etc/passwd"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveGithubReleases, "a b/c"));
  EXPECT_FALSE(LiveFolderFeedUrl("githubPulls", "o/r"));
  EXPECT_FALSE(LiveFolderFeedUrl(kLiveFeed, std::string(5000, 'a')));

  EXPECT_EQ(LiveFolderName(kLiveGithubReleases,
                           GURL("https://github.com/o/r/releases.atom")),
            "o/r Releases");
  EXPECT_EQ(LiveFolderName(kLiveFeed, GURL("https://www.news.test/feed")),
            "news.test");
}

TEST(CluLiveFeedTest, PlanPutsNewEntriesOnTopAndKeepsOpenRows) {
  const std::vector<LiveRow> rows = {{"r1", "https://a.test/1", false},
                                     {"r2", "https://a.test/2", false},
                                     {"r3", "https://a.test/3", true}};
  const std::vector<LiveFeedItem> items = {{"https://a.test/5", "five"},
                                           {"https://a.test/4", "four"},
                                           {"https://a.test/1", "one"}};
  const LiveFolderPlan plan = PlanLiveFolder(rows, items);
  // 2 left the feed; 3 left too but its page is open.
  EXPECT_EQ(plan.remove, (std::vector<std::string>{"r2"}));
  ASSERT_EQ(plan.insert.size(), 2u);
  EXPECT_EQ(plan.insert[0], (std::pair<int, LiveFeedItem>{0, items[0]}));
  EXPECT_EQ(plan.insert[1], (std::pair<int, LiveFeedItem>{1, items[1]}));
  // Nothing new: nothing to do.
  const LiveFolderPlan same = PlanLiveFolder(
      {{"r5", "https://a.test/5", false}}, {{"https://a.test/5", "five"}});
  EXPECT_TRUE(same.remove.empty());
  EXPECT_TRUE(same.insert.empty());
}

// A Gmail Atom feed entry, as the data decoder returns it.
std::string MailEntry(std::string_view title,
                      std::string_view author,
                      std::string_view issued,
                      std::string_view href) {
  return base::StrCat(
      {R"({"type":"element","tag":"entry","children":[)", Text("title", title),
       ",", Text("issued", issued), ",",
       R"({"type":"element","tag":"link","attributes":{"rel":"alternate","href":")",
       href, R"("}},)",
       R"({"type":"element","tag":"author","children":[)", Text("name", author),
       ",", Text("email", "a@mail.test"), "]}]}"});
}

std::string MailFeed(std::string_view fullcount, std::string_view entries) {
  return base::StrCat(
      {R"({"type":"element","tag":"feed","attributes":{"version":"0.3"},"children":[)",
       Text("title", "Gmail - Inbox for test@mail.test"), ",",
       Text("fullcount", fullcount), entries.empty() ? "" : ",", entries,
       "]}"});
}

TEST(CluLiveFeedTest, GmailFeedWithNoUnreadMail) {
  const std::optional<GmailUnread> unread =
      ParseGmailFeed(Xml(MailFeed("0", "")));
  ASSERT_TRUE(unread);
  EXPECT_EQ(unread->count, 0);
  EXPECT_TRUE(unread->items.empty());
}

TEST(CluLiveFeedTest, GmailFeedListsUnreadMailNewestFirst) {
  const std::string feed = MailFeed(
      "3",
      base::StrCat(
          {MailEntry("Older", "Ann", "2026-11-10T08:00:00Z",
                     "https://mail.google.com/mail?message_id=1&amp;view=conv"),
           ",",
           MailEntry("Newest", "Bo", "2026-11-10T19:59:00Z",
                     "https://mail.google.com/mail?message_id=2"),
           ",",
           // A link off Gmail opens nothing; the message still lists.
           MailEntry("Middle", "Cy", "2026-11-10T12:00:00Z",
                     "https://evil.test/phish")}));
  const std::optional<GmailUnread> unread = ParseGmailFeed(Xml(feed));
  ASSERT_TRUE(unread);
  EXPECT_EQ(unread->count, 3);
  ASSERT_EQ(unread->items.size(), 3u);
  EXPECT_EQ(unread->items[0],
            (GmailUnreadItem{"Bo", "Newest", "2026-11-10T19:59:00Z",
                             "https://mail.google.com/mail?message_id=2"}));
  EXPECT_EQ(unread->items[1].title, "Middle");
  EXPECT_EQ(unread->items[1].url, "");
  EXPECT_EQ(unread->items[2].author, "Ann");
}

TEST(CluLiveFeedTest, GmailFeedKeepsTheFullCountAndFiveNewest) {
  std::string entries;
  for (int i = 0; i < 20; ++i) {
    const std::string n = base::NumberToString(10 + i);
    entries += base::StrCat(
        {i ? "," : "",
         MailEntry("Mail " + n, "Sender " + n,
                   "2026-11-10T" + n + ":00:00Z",
                   "https://mail.google.com/mail?message_id=" + n)});
  }
  const std::optional<GmailUnread> unread =
      ParseGmailFeed(Xml(MailFeed("40", entries)));
  ASSERT_TRUE(unread);
  EXPECT_EQ(unread->count, 40);
  ASSERT_EQ(unread->items.size(), kMaxGmailUnreadItems);
  EXPECT_EQ(unread->items.front().title, "Mail 29");
  EXPECT_EQ(unread->items.back().title, "Mail 25");
  // A count below the entries listed is the entries'.
  EXPECT_EQ(ParseGmailFeed(Xml(MailFeed("1", entries)))->count, 20);
}

TEST(CluLiveFeedTest, GmailFeedRejectsAnythingElse) {
  // A sign-in page, an RSS feed, a feed without or with a junk count.
  EXPECT_FALSE(ParseGmailFeed(Xml(
      R"({"type":"element","tag":"html","children":[]})")));
  EXPECT_FALSE(ParseGmailFeed(Xml(base::StrCat(
      {R"({"type":"element","tag":"rss","children":[)", Text("fullcount", "3"),
       "]}"}))));
  EXPECT_FALSE(ParseGmailFeed(Xml(
      R"({"type":"element","tag":"feed","children":[]})")));
  EXPECT_FALSE(ParseGmailFeed(Xml(MailFeed("lots", ""))));
  EXPECT_FALSE(ParseGmailFeed(Xml(MailFeed("-2", ""))));
  EXPECT_FALSE(ParseGmailFeed(base::Value()));
}

TEST(CluLiveFeedTest, GmailFeedUrlFollowsTheAccount) {
  EXPECT_EQ(GmailFeedUrl("https://mail.google.com/mail/u/1/#inbox"),
            GURL("https://mail.google.com/mail/u/1/feed/atom"));
  EXPECT_EQ(GmailFeedUrl("https://mail.google.com/mail/#inbox"),
            GURL("https://mail.google.com/mail/u/0/feed/atom"));
  // An address or authuser= reads that account's feed; a selector that names no readable
  // account reads none, never account 0's.
  EXPECT_EQ(GmailFeedUrl("https://mail.google.com/mail/u/Work@Company.com/#inbox"),
            GURL("https://mail.google.com/mail/u/work@company.com/feed/atom"));
  EXPECT_EQ(GmailFeedUrl("https://mail.google.com/mail/u/work%40company.com/"),
            GURL("https://mail.google.com/mail/u/work@company.com/feed/atom"));
  EXPECT_EQ(GmailFeedUrl("https://mail.google.com/mail/?authuser=2#inbox"),
            GURL("https://mail.google.com/mail/u/2/feed/atom"));
  EXPECT_EQ(
      GmailFeedUrl("https://mail.google.com/mail/?authuser=work%40company.com"),
      GURL("https://mail.google.com/mail/u/work@company.com/feed/atom"));
  EXPECT_EQ(GmailFeedUrl("https://mail.google.com/mail/u/1/?authuser=1"),
            GURL("https://mail.google.com/mail/u/1/feed/atom"));
  for (const char* junk :
       {"https://mail.google.com/mail/u/123/",
        "https://mail.google.com/mail/u/abc/",
        "https://mail.google.com/mail/u//",
        "https://mail.google.com/mail/u/a@b@c/",
        "https://mail.google.com/mail/u/%3Cscript%3E@x.com/",
        "https://mail.google.com/mail/?authuser=",
        "https://mail.google.com/mail/?authuser=../../x",
        "https://mail.google.com/mail/u/1/?authuser=0",
        "https://mail.google.com/mail/u/0/?authuser=me@x.com"}) {
    EXPECT_FALSE(GmailFeedUrl(junk)) << junk;
  }
  EXPECT_FALSE(GmailFeedUrl("https://calendar.google.com/calendar/u/1/r"));
  EXPECT_FALSE(GmailFeedUrl("javascript:alert(1)"));
  EXPECT_FALSE(GmailFeedUrl("not a url"));
}

}  // namespace
}  // namespace clu
