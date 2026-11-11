// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_download_rows.h"

#include <string>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

// The rules of the downloads rows (docs/chromium/m9.md), plus junk from the item side.
namespace clu {
namespace {

constexpr char kGuid[] = "6f1c2a8e-3b4d-4e5f-9a6b-7c8d9e0f1a2b";
constexpr char kProfile[] = "0b6e0c38-1f0e-4a8f-9d55-3a0c0f6b8e21";

DownloadFacts Item(std::string guid = kGuid) {
  DownloadFacts facts;
  facts.guid = std::move(guid);
  facts.file_name = "report.pdf";
  facts.url = "https://example.com/report.pdf";
  facts.phase = DownloadPhase::kProgressing;
  facts.received_bytes = 400;
  facts.total_bytes = 1000;
  facts.bytes_per_second = 200;
  facts.remaining = base::Seconds(3);
  facts.start_time = base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000);
  return facts;
}

TEST(CluDownloadRowsTest, DisplayNamesAreOneSafeSegment) {
  EXPECT_EQ(DisplayFileName("report.pdf"), "report.pdf");
  // Separators and dot segments go; the rest join with "_".
  EXPECT_EQ(DisplayFileName("../../etc/passwd"), "etc_passwd");
  EXPECT_EQ(DisplayFileName("a\\b/c.txt"), "a_b_c.txt");
  // Control, bidi and zero-width characters go; reserved ones become "_".
  EXPECT_EQ(DisplayFileName("in\x01voice\x7f.pdf"), "invoice.pdf");
  EXPECT_EQ(DisplayFileName("cod‮gpj.exe"), "codgpj.exe");
  EXPECT_EQ(DisplayFileName("a:b*c?.txt"), "a_b_c_.txt");
  // No leading dots, no trailing dots or spaces.
  EXPECT_EQ(DisplayFileName(".hidden"), "hidden");
  EXPECT_EQ(DisplayFileName("  name.txt.  "), "name.txt");
  // Nothing left: the fallback.
  EXPECT_EQ(DisplayFileName(""), "download");
  EXPECT_EQ(DisplayFileName("../.."), "download");
  EXPECT_EQ(DisplayFileName("\x01\x02"), "download");
  // Invalid UTF-8 is dropped, valid non-ASCII stays.
  EXPECT_EQ(DisplayFileName("caf\xC3\xA9\xFF.txt"), "caf\xC3\xA9.txt");
}

TEST(CluDownloadRowsTest, LongNamesKeepTheirExtension) {
  const std::string base(1000, 'a');
  const std::string capped = DisplayFileName(base + ".pdf");
  EXPECT_EQ(capped.size(), kMaxDownloadNameLength);
  EXPECT_TRUE(capped.ends_with(".pdf"));
  // An "extension" longer than 16 characters is part of the name.
  const std::string long_ext = DisplayFileName("a." + std::string(300, 'b'));
  EXPECT_EQ(long_ext.size(), kMaxDownloadNameLength);
  EXPECT_TRUE(long_ext.starts_with("a.b"));
  // Code points, not bytes: 300 two-byte characters cap at 200 characters.
  std::string accents;
  for (int i = 0; i < 300; ++i) {
    accents += "\xC3\xA9";
  }
  EXPECT_EQ(DisplayFileName(accents).size(), kMaxDownloadNameLength * 2);
}

TEST(CluDownloadRowsTest, ARowCarriesTheStateFields) {
  const std::optional<base::DictValue> row = DownloadRow(Item(), kProfile);
  ASSERT_TRUE(row);
  EXPECT_EQ(*row->FindString("id"), kGuid);
  EXPECT_EQ(*row->FindString("profileId"), kProfile);
  EXPECT_EQ(*row->FindString("filename"), "report.pdf");
  EXPECT_EQ(*row->FindString("url"), "https://example.com/report.pdf");
  EXPECT_EQ(*row->FindString("state"), "progressing");
  EXPECT_EQ(row->FindDouble("receivedBytes"), 400);
  EXPECT_EQ(row->FindDouble("totalBytes"), 1000);
  EXPECT_EQ(row->FindDouble("bytesPerSecond"), 200);
  EXPECT_EQ(row->FindDouble("etaMs"), 3000);
  EXPECT_EQ(row->FindDouble("startedAt"), 1'700'000'000'000);
  ASSERT_TRUE(row->Find("endedAt"));
  EXPECT_TRUE(row->Find("endedAt")->is_none());
  EXPECT_EQ(row->FindBool("exists"), true);
}

TEST(CluDownloadRowsTest, IndeterminateAndStoppedRowsShowNoRateOrTimeLeft) {
  DownloadFacts unknown = Item();
  unknown.total_bytes = -1;
  std::optional<base::DictValue> row = DownloadRow(unknown, kProfile);
  ASSERT_TRUE(row);
  EXPECT_EQ(row->FindDouble("totalBytes"), 0);
  EXPECT_TRUE(row->Find("etaMs")->is_none());

  DownloadFacts paused = Item();
  paused.phase = DownloadPhase::kPaused;
  row = DownloadRow(paused, kProfile);
  EXPECT_EQ(*row->FindString("state"), "paused");
  EXPECT_EQ(row->FindDouble("bytesPerSecond"), 0);
  EXPECT_TRUE(row->Find("etaMs")->is_none());

  DownloadFacts done = Item();
  done.phase = DownloadPhase::kCompleted;
  done.received_bytes = 1000;
  done.end_time = done.start_time + base::Seconds(5);
  row = DownloadRow(done, kProfile);
  EXPECT_EQ(*row->FindString("state"), "completed");
  EXPECT_EQ(row->FindDouble("endedAt"), 1'700'000'005'000);
  EXPECT_EQ(row->FindBool("exists"), true);

  // A finished file gone from disk shows "Removed"; a running one cannot be gone.
  done.externally_removed = true;
  EXPECT_EQ(DownloadRow(done, kProfile)->FindBool("exists"), false);
  DownloadFacts running = Item();
  running.externally_removed = true;
  EXPECT_EQ(DownloadRow(running, kProfile)->FindBool("exists"), true);

  // A negative or infinite time left is unknown.
  DownloadFacts odd = Item();
  odd.remaining = base::Seconds(-2);
  EXPECT_TRUE(DownloadRow(odd, kProfile)->Find("etaMs")->is_none());
  odd.remaining = base::TimeDelta::Max();
  EXPECT_TRUE(DownloadRow(odd, kProfile)->Find("etaMs")->is_none());

  for (DownloadPhase phase :
       {DownloadPhase::kCancelled, DownloadPhase::kInterrupted}) {
    DownloadFacts stopped = Item();
    stopped.phase = phase;
    EXPECT_EQ(*DownloadRow(stopped, kProfile)->FindString("state"),
              PhaseName(phase));
  }
}

TEST(CluDownloadRowsTest, JunkCountsAndIdsNeverShowOrThrow) {
  DownloadFacts junk = Item();
  junk.received_bytes = -50;
  junk.bytes_per_second = -9;
  std::optional<base::DictValue> row = DownloadRow(junk, kProfile);
  ASSERT_TRUE(row);
  EXPECT_EQ(row->FindDouble("receivedBytes"), 0);
  EXPECT_EQ(row->FindDouble("bytesPerSecond"), 0);
  // More received than the server announced: capped at the total.
  junk = Item();
  junk.received_bytes = 5000;
  EXPECT_EQ(DownloadRow(junk, kProfile)->FindDouble("receivedBytes"), 1000);
  // Unknown total: received bytes stay as they are.
  junk.total_bytes = 0;
  EXPECT_EQ(DownloadRow(junk, kProfile)->FindDouble("receivedBytes"), 5000);

  // download.act addresses rows by id, and the guard accepts only UUID v4s.
  EXPECT_FALSE(DownloadRow(Item(""), kProfile));
  EXPECT_FALSE(DownloadRow(Item("not-a-guid"), kProfile));
  EXPECT_FALSE(DownloadRow(Item("6f1c2a8e-3b4d-1e5f-9a6b-7c8d9e0f1a2b"), kProfile));
  // Upper-case hex is still a UUID.
  EXPECT_TRUE(DownloadRow(Item("6F1C2A8E-3B4D-4E5F-9A6B-7C8D9E0F1A2B"), kProfile));

  // Transient and temporary items never show.
  DownloadFacts hidden = Item();
  hidden.hidden = true;
  EXPECT_FALSE(DownloadRow(hidden, kProfile));

  // A junk name falls back.
  DownloadFacts nameless = Item();
  nameless.file_name = "/";
  EXPECT_EQ(*DownloadRow(nameless, kProfile)->FindString("filename"),
            "download");
}

TEST(CluDownloadRowsTest, RowsAreNewestFirstAndCapped) {
  std::vector<DownloadFacts> items;
  for (int i = 0; i < 250; ++i) {
    DownloadFacts item = Item(base::StrCat(
        {"6f1c2a8e-3b4d-4e5f-9a6b-", i < 10 ? "00000000000" : i < 100 ? "0000000000" : "000000000",
         base::NumberToString(i)}));
    item.start_time += base::Seconds(i);
    items.push_back(std::move(item));
  }
  // Junk in the middle is skipped, not counted.
  items.push_back(Item("junk"));
  const base::ListValue rows = DownloadRows(items, kProfile);
  ASSERT_EQ(rows.size(), kMaxDownloadRows);
  EXPECT_EQ(*rows[0].GetDict().FindString("id"),
            "6f1c2a8e-3b4d-4e5f-9a6b-000000000249");
  EXPECT_EQ(*rows[kMaxDownloadRows - 1].GetDict().FindString("id"),
            "6f1c2a8e-3b4d-4e5f-9a6b-000000000050");

  // Equal start times order by id, so the list never reshuffles between patches.
  DownloadFacts a = Item("6f1c2a8e-3b4d-4e5f-9a6b-00000000000a");
  DownloadFacts b = Item("6f1c2a8e-3b4d-4e5f-9a6b-00000000000b");
  const base::ListValue tied = DownloadRows({b, a}, kProfile);
  EXPECT_EQ(*tied[0].GetDict().FindString("id"), a.guid);
  EXPECT_TRUE(DownloadRows({}, kProfile).empty());
}

// Screenshot arrivals reach state newest first, capped, with junk names and kinds left out.
TEST(CluDownloadRowsTest, ScreenshotArrivalsAreNewestFirstAndClean) {
  const base::Time t = base::Time::FromMillisecondsSinceUnixEpoch(1'700'000'000'000);
  std::vector<ScreenshotArrival> arrivals = {
      {"Screenshot 1.png", "image", t},
      {"Recording.mov", "video", t + base::Seconds(2)},
      {"", "image", t},
      {"../escape.png", "image", t},
      {".hidden.png", "image", t},
      {"notes.txt", "file", t},
  };
  base::ListValue rows = ScreenshotArrivalRows(arrivals);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(*rows[0].GetDict().FindString("name"), "Recording.mov");
  EXPECT_EQ(*rows[0].GetDict().FindString("kind"), "video");
  EXPECT_EQ(*rows[1].GetDict().FindDouble("at"),
            t.InMillisecondsFSinceUnixEpoch());
  for (int i = 0; i < 9; ++i) {
    arrivals.push_back({base::StrCat({"s", base::NumberToString(i), ".png"}),
                        "image", t + base::Seconds(10 + i)});
  }
  rows = ScreenshotArrivalRows(arrivals);
  ASSERT_EQ(rows.size(), kMaxScreenshotArrivals);
  EXPECT_EQ(*rows[0].GetDict().FindString("name"), "s8.png");
  EXPECT_TRUE(ScreenshotArrivalRows({}).empty());
}

}  // namespace
}  // namespace clu
