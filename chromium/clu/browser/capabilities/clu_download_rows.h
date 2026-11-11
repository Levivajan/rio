// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_DOWNLOAD_ROWS_H_
#define CLU_BROWSER_CAPABILITIES_CLU_DOWNLOAD_ROWS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

// The downloads rows of CluState (DownloadState in src/shared/state/capabilities.ts), built from plain facts about
// Chromium's download items so the rules are testable without a profile (docs/chromium/m9.md). Chromium owns the
// downloads themselves: the file names on disk, the folder, resumption, the danger checks and the bubble. These rows
// feed the sidebar's downloads button and its progress ring.
namespace clu {

// At most this many rows, newest first.
inline constexpr size_t kMaxDownloadRows = 200;
// A shown file name: at most 200 code points, extension kept.
inline constexpr size_t kMaxDownloadNameLength = 200;
// Updates of a running download reach state at most 4 times a second (docs/plan/06-engine.md 2.10).
inline constexpr base::TimeDelta kDownloadUpdateInterval = base::Milliseconds(250);

// download::DownloadItem::DownloadState plus IsPaused().
enum class DownloadPhase { kProgressing, kPaused, kCompleted, kCancelled, kInterrupted };

struct DownloadFacts {
  DownloadFacts();
  DownloadFacts(const DownloadFacts&);
  DownloadFacts& operator=(const DownloadFacts&);
  ~DownloadFacts();

  std::string guid;
  // GetFileNameToReportUser(), UTF-8. May be empty before the target is known.
  std::string file_name;
  // GetURL(), as state carries URLs (StateUrl).
  std::string url;
  DownloadPhase phase = DownloadPhase::kProgressing;
  int64_t received_bytes = 0;
  // GetTotalBytes(): 0 or less when the server sent no length.
  int64_t total_bytes = 0;
  // CurrentSpeed(), bytes per second.
  int64_t bytes_per_second = 0;
  // TimeRemaining(), when Chromium knows it.
  std::optional<base::TimeDelta> remaining;
  base::Time start_time;
  // Null while running.
  base::Time end_time;
  bool externally_removed = false;
  // Transient and temporary items (drags, extension downloads with no shelf, save-as temp files) never show.
  bool hidden = false;
};

// A file name to show: one path segment, no control, bidi or zero-width characters, reserved characters as "_", no
// leading dots or trailing dots and spaces, at most kMaxDownloadNameLength code points with the extension kept;
// "download" when nothing is left.
std::string DisplayFileName(std::string_view raw);

// "progressing", "paused", "completed", "cancelled" or "interrupted".
std::string_view PhaseName(DownloadPhase phase);

// One DownloadState row, or nullopt for an item that must not show: hidden items and items whose guid is not a
// UUID v4 (download.act addresses rows by that id, and the bridge guard accepts only UUIDs). Negative counts read
// as 0, received bytes never exceed a known total, the rate and time left show only while progressing.
std::optional<base::DictValue> DownloadRow(const DownloadFacts& facts,
                                           std::string_view profile_id);

// Every showable row, newest first by start time (ties by guid, so the order is stable), at most kMaxDownloadRows.
base::ListValue DownloadRows(std::vector<DownloadFacts> facts,
                             std::string_view profile_id);

// A screenshot or screen recording that landed in the screenshot folder while Rio runs: the sidebar shows
// it as a card above the bottom bar. `name` is the bare file name download.fileAct opens with source media.
struct ScreenshotArrival {
  std::string name;
  // "image" or "video".
  std::string kind;
  base::Time modified;
};

// At most this many arrivals in state, the newest.
inline constexpr size_t kMaxScreenshotArrivals = 5;

// The screenshotArrivals rows of state: [{name, kind, at}], newest first, at most kMaxScreenshotArrivals. Rows with
// an empty name, a name with a separator or a leading dot, or a kind other than image or video are left out.
base::ListValue ScreenshotArrivalRows(
    const std::vector<ScreenshotArrival>& arrivals);

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_DOWNLOAD_ROWS_H_
