// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_download_rows.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/strings/utf_string_conversion_utils.h"
#include "clu/browser/ui/webui/bridge/validation.h"

namespace clu {

namespace {

constexpr char kFallbackName[] = "download";
// Longer "extensions" are part of the name (names.ts splitExt).
constexpr size_t kMaxExtensionLength = 16;

// Control characters, DEL and C1, and the bidi and zero-width characters that can disguise an extension.
bool IsInvisible(base_icu::UChar32 c) {
  return c <= 0x1f || (c >= 0x7f && c <= 0x9f) || (c >= 0x200b && c <= 0x200f) ||
         (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069) ||
         c == 0xfeff;
}

bool IsReserved(base_icu::UChar32 c) {
  return c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
         c == '>' || c == '|';
}

bool IsTrimmed(base_icu::UChar32 c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

// The code points of `raw`, with invalid UTF-8 dropped.
std::vector<base_icu::UChar32> CodePoints(std::string_view raw) {
  std::vector<base_icu::UChar32> out;
  for (size_t i = 0; i < raw.size(); ++i) {
    base_icu::UChar32 c = 0;
    if (base::ReadUnicodeCharacter(raw, &i, &c) && base::IsValidCodepoint(c)) {
      out.push_back(c);
    }
  }
  return out;
}

std::string ToUtf8(const std::vector<base_icu::UChar32>& chars) {
  std::string out;
  for (base_icu::UChar32 c : chars) {
    base::WriteUnicodeCharacter(c, &out);
  }
  return out;
}

int64_t NonNegative(int64_t value) {
  return std::max<int64_t>(0, value);
}

}  // namespace

DownloadFacts::DownloadFacts() = default;
DownloadFacts::DownloadFacts(const DownloadFacts&) = default;
DownloadFacts& DownloadFacts::operator=(const DownloadFacts&) = default;
DownloadFacts::~DownloadFacts() = default;

std::string DisplayFileName(std::string_view raw) {
  // Long junk is cut before any work, as names.ts does at 4096.
  std::vector<base_icu::UChar32> chars =
      CodePoints(raw.substr(0, std::min<size_t>(raw.size(), 4096 * 4)));
  if (chars.size() > 4096) {
    chars.resize(4096);
  }
  std::erase_if(chars, IsInvisible);

  // Path segments: separators go, as do empty, "." and ".." segments; the rest join with "_".
  std::vector<std::vector<base_icu::UChar32>> segments(1);
  for (base_icu::UChar32 c : chars) {
    if (c == '/' || c == '\\') {
      segments.emplace_back();
    } else {
      segments.back().push_back(c);
    }
  }
  std::vector<base_icu::UChar32> joined;
  for (std::vector<base_icu::UChar32>& segment : segments) {
    while (!segment.empty() && IsTrimmed(segment.front())) {
      segment.erase(segment.begin());
    }
    while (!segment.empty() && IsTrimmed(segment.back())) {
      segment.pop_back();
    }
    const bool dots = segment == std::vector<base_icu::UChar32>{'.'} ||
                      segment == std::vector<base_icu::UChar32>{'.', '.'};
    if (segment.empty() || dots) {
      continue;
    }
    if (!joined.empty()) {
      joined.push_back('_');
    }
    joined.insert(joined.end(), segment.begin(), segment.end());
  }
  for (base_icu::UChar32& c : joined) {
    if (IsReserved(c)) {
      c = '_';
    }
  }
  // No leading dots or spaces, no trailing dots or spaces.
  while (!joined.empty() && (joined.front() == '.' || IsTrimmed(joined.front()))) {
    joined.erase(joined.begin());
  }
  while (!joined.empty() && (joined.back() == '.' || IsTrimmed(joined.back()))) {
    joined.pop_back();
  }
  if (joined.empty()) {
    return kFallbackName;
  }
  if (joined.size() <= kMaxDownloadNameLength) {
    return ToUtf8(joined);
  }
  // Over the cap: the base shrinks, the extension stays.
  const auto dot = std::find(joined.rbegin(), joined.rend(), '.');
  size_t ext_length = 0;
  if (dot != joined.rend()) {
    ext_length = static_cast<size_t>(dot - joined.rbegin()) + 1;
    if (ext_length > kMaxExtensionLength || ext_length == joined.size()) {
      ext_length = 0;
    }
  }
  const size_t base_length = std::max<size_t>(1, kMaxDownloadNameLength - ext_length);
  std::vector<base_icu::UChar32> capped(joined.begin(),
                                        joined.begin() + base_length);
  capped.insert(capped.end(), joined.end() - ext_length, joined.end());
  return ToUtf8(capped);
}

std::string_view PhaseName(DownloadPhase phase) {
  switch (phase) {
    case DownloadPhase::kProgressing:
      return "progressing";
    case DownloadPhase::kPaused:
      return "paused";
    case DownloadPhase::kCompleted:
      return "completed";
    case DownloadPhase::kCancelled:
      return "cancelled";
    case DownloadPhase::kInterrupted:
      return "interrupted";
  }
  return "interrupted";
}

std::optional<base::DictValue> DownloadRow(const DownloadFacts& facts,
                                           std::string_view profile_id) {
  if (facts.hidden || !IsUuidV4(facts.guid)) {
    return std::nullopt;
  }
  const int64_t total = NonNegative(facts.total_bytes);
  int64_t received = NonNegative(facts.received_bytes);
  if (total > 0) {
    received = std::min(received, total);
  }
  const bool progressing = facts.phase == DownloadPhase::kProgressing;
  base::Value eta;
  if (progressing && total > 0 && facts.remaining &&
      !facts.remaining->is_negative() && !facts.remaining->is_inf()) {
    eta = base::Value(static_cast<double>(facts.remaining->InMilliseconds()));
  }
  base::Value ended;
  if (!facts.end_time.is_null()) {
    ended = base::Value(facts.end_time.InMillisecondsFSinceUnixEpoch());
  }
  base::DictValue row;
  row.Set("id", facts.guid);
  row.Set("profileId", profile_id);
  row.Set("filename", DisplayFileName(facts.file_name));
  row.Set("url", facts.url);
  row.Set("state", PhaseName(facts.phase));
  row.Set("receivedBytes", static_cast<double>(received));
  row.Set("totalBytes", static_cast<double>(total));
  row.Set("bytesPerSecond",
          progressing ? static_cast<double>(NonNegative(facts.bytes_per_second))
                      : 0.0);
  row.Set("etaMs", std::move(eta));
  row.Set("startedAt", facts.start_time.InMillisecondsFSinceUnixEpoch());
  row.Set("endedAt", std::move(ended));
  // Only a finished file can be gone from disk.
  row.Set("exists", !(facts.phase == DownloadPhase::kCompleted &&
                      facts.externally_removed));
  return row;
}

base::ListValue DownloadRows(std::vector<DownloadFacts> facts,
                             std::string_view profile_id) {
  std::ranges::sort(facts, [](const DownloadFacts& a, const DownloadFacts& b) {
    if (a.start_time != b.start_time) {
      return a.start_time > b.start_time;
    }
    return a.guid < b.guid;
  });
  base::ListValue rows;
  for (const DownloadFacts& item : facts) {
    if (rows.size() >= kMaxDownloadRows) {
      break;
    }
    if (std::optional<base::DictValue> row = DownloadRow(item, profile_id)) {
      rows.Append(std::move(*row));
    }
  }
  return rows;
}

base::ListValue ScreenshotArrivalRows(
    const std::vector<ScreenshotArrival>& arrivals) {
  std::vector<const ScreenshotArrival*> shown;
  for (const ScreenshotArrival& arrival : arrivals) {
    if (arrival.name.empty() || arrival.name.starts_with(".") ||
        arrival.name.find('/') != std::string::npos ||
        (arrival.kind != "image" && arrival.kind != "video")) {
      continue;
    }
    shown.push_back(&arrival);
  }
  std::ranges::stable_sort(shown, [](const ScreenshotArrival* a,
                                     const ScreenshotArrival* b) {
    return a->modified > b->modified;
  });
  base::ListValue rows;
  for (const ScreenshotArrival* arrival : shown) {
    if (rows.size() == kMaxScreenshotArrivals) {
      break;
    }
    rows.Append(base::DictValue()
                    .Set("name", arrival->name)
                    .Set("kind", arrival->kind)
                    .Set("at", arrival->modified
                                   .InMillisecondsFSinceUnixEpochIgnoringNull()));
  }
  return rows;
}

}  // namespace clu
