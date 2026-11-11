// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_tidy.h"

#include <optional>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Tidy Tab Titles and Tidy Downloads, on sample titles and file names ("Interior Design Book", "387494876.pdf"
// to "Navan Itinerary.pdf") and the junk real pages send.
namespace clu {
namespace {

std::string Tidy(std::string_view title, std::string_view url) {
  return TidyPinnedTitle(title, GURL(url)).value_or("(kept)");
}

TEST(CluTidyTest, PinnedTitlesDropTheSiteNameAndCounts) {
  EXPECT_EQ(Tidy("Interior Design Book - Google Docs",
                 "https://docs.google.com/document/d/1"),
            "Interior Design Book");
  EXPECT_EQ(Tidy("(3) Never Gonna Give You Up - YouTube",
                 "https://www.youtube.com/watch?v=1"),
            "Never Gonna Give You Up");
  EXPECT_EQ(Tidy("Inbox (1,234) - user@example.com - Gmail",
                 "https://mail.google.com/mail/u/0/"),
            "Inbox");
  EXPECT_EQ(Tidy("GitHub \xC2\xB7 example/project",
                 "https://github.com/example/project"),
            "example/project");
  EXPECT_EQ(Tidy("Pricing | Linear", "https://linear.app/pricing"),
            "Pricing");
  EXPECT_EQ(Tidy("Weeknight soup \xE2\x80\x94 Recipes Daily",
                 "https://recipes.example.org/soup"),
            "Weeknight soup");
}

TEST(CluTidyTest, PinnedTitlesCutLongTitlesAtAWord) {
  const std::string tidy =
      Tidy("Verify it\xE2\x80\x99s you To help keep your account safe, "
           "Google wants to make sure it\xE2\x80\x99s really you",
           "https://accounts.google.com/signin");
  EXPECT_EQ(tidy, "Verify it\xE2\x80\x99s you To help keep your");
  // A long word without spaces is cut hard, never inside a surrogate pair.
  const std::string emoji = std::string(39, 'a') + "\xF0\x9F\x98\x80" + "bbb";
  EXPECT_EQ(Tidy(emoji, "https://a.test/"), std::string(39, 'a'));
}

TEST(CluTidyTest, PinnedTitlesKeepTidyOnesAndJunk) {
  EXPECT_EQ(Tidy("GitHub", "https://github.com/"), "(kept)");
  EXPECT_EQ(Tidy("Dashboard", "https://app.example.com/"), "(kept)");
  EXPECT_EQ(Tidy("example.com", "https://example.com/"), "(kept)");
  EXPECT_EQ(Tidy("https://a.test/x", "https://a.test/x"), "(kept)");
  EXPECT_EQ(Tidy("", "https://a.test/"), "(kept)");
  EXPECT_EQ(Tidy("   \n\t ", "https://a.test/"), "(kept)");
  EXPECT_EQ(Tidy("\xFF\xFE bad utf8", "https://a.test/"), "(kept)");
  EXPECT_EQ(Tidy(" - | - ", "https://a.test/"), "(kept)");
  // No URL: the parts are judged without a site.
  EXPECT_EQ(Tidy("Notes - Draft", ""), "Notes");
}

std::string Name(std::string_view file,
                 std::string_view title,
                 std::string_view url = "https://travel.example.com/trip") {
  return TidyDownloadName(file, title, GURL(url)).value_or("(kept)");
}

TEST(CluTidyTest, JunkDownloadNamesTakeThePageTitle) {
  EXPECT_EQ(Name("387494876.pdf", "Navan Itinerary - Navan"),
            "Navan Itinerary.pdf");
  EXPECT_EQ(Name("a3f9c2e1-7b4d-4e2a-9c1f-0d2e3f4a5b6c.png", "Team photo"),
            "Team photo.png");
  // The site's name goes; a colon cannot stay in a file name.
  EXPECT_EQ(Name("download (2).zip", "Release 1.4: assets | Example"),
            "Release 1.4 assets.zip");
  EXPECT_EQ(Name("file.csv", "Q3 numbers / sales?"), "Q3 numbers sales.csv");
  EXPECT_EQ(Name("export_12.xlsx", "Pipeline (4) - CRM"), "Pipeline.xlsx");
  EXPECT_EQ(Name("document", "Lease agreement"), "Lease agreement");
}

TEST(CluTidyTest, GoodDownloadNamesStay) {
  EXPECT_EQ(Name("Navan Itinerary.pdf", "Something else"), "(kept)");
  EXPECT_EQ(Name("report-final.pdf", "Q3"), "(kept)");
  EXPECT_EQ(Name("abc.pdf", "Q3"), "(kept)");
  EXPECT_EQ(Name("deadbeef.pdf", "Q3"), "(kept)");  // hex, but no digit
  EXPECT_EQ(Name("387494876.pdf", ""), "(kept)");
  EXPECT_EQ(Name("387494876.pdf", "   "), "(kept)");
  EXPECT_EQ(Name("387494876.pdf", "..."), "(kept)");
  EXPECT_EQ(Name("", "Title"), "(kept)");
  EXPECT_EQ(Name("\xFF.pdf", "Title"), "(kept)");
}

TEST(CluTidyTest, ModelTopicsBecomeTidyOrder) {
  // Tabs of one topic (any case) together, the biggest group first, each group in the tabs' own order, a tab the
  // answer left out at the end.
  EXPECT_EQ(TidyOrderFromModel("1: Rust | 2: Rio | 3: Python | 4: rio | 5: News | "
                               "6: Python | 7: Rio.",
                               8),
            (std::vector<int>{1, 3, 6, 2, 5, 0, 4, 7}));
  EXPECT_EQ(TidyOrderFromModel(" 1: A|2: B |3: a", 3), (std::vector<int>{0, 2, 1}));
}

TEST(CluTidyTest, ModelTopicsRefuseJunk) {
  EXPECT_FALSE(TidyOrderFromModel("", 3));
  EXPECT_FALSE(TidyOrderFromModel("1: Rio | 2: Rio | 3: Rio", 3));  // one topic: nothing to tidy
  EXPECT_FALSE(TidyOrderFromModel("1: Rio | 1: Python", 3));        // a tab twice
  EXPECT_FALSE(TidyOrderFromModel("1: Rio | 4: Python", 3));        // out of range
  EXPECT_FALSE(TidyOrderFromModel("0: Rio | 1: Python", 3));        // numbers start at 1
  EXPECT_FALSE(TidyOrderFromModel("Here you go: 1,2|3", 3));
  EXPECT_FALSE(TidyOrderFromModel("1: | 2: Python", 3));            // no topic
  EXPECT_FALSE(TidyOrderFromModel("1: A | 2: B", 0));
  // Every tab its own topic (the model copied the titles): nothing to group.
  EXPECT_FALSE(TidyOrderFromModel("1: Sugarloaf Mountain | 2: docs.python.org | 3: Wikipedia", 3));
}

}  // namespace
}  // namespace clu
