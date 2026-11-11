// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_index_sync.h"

#include <algorithm>
#include <set>

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "url/gurl.h"

namespace clu {

namespace {

// The longest URL the bridge carries (MAX_URL in src/shared/schema.ts).
constexpr size_t kMaxIndexUrl = 32 * 1024;

int64_t g_rev_counter = 0;

std::string CleanTitle(std::u16string_view title) {
  std::u16string out;
  out.reserve(std::min(title.size(), kMaxIndexTitle));
  for (char16_t c : title) {
    if (out.size() == kMaxIndexTitle) {
      break;
    }
    // Control characters never reach the list; a tab or newline reads as a space.
    if (base::IsUnicodeControl(c)) {
      if (base::IsAsciiWhitespace(c)) {
        out.push_back(u' ');
      }
      continue;
    }
    out.push_back(c);
  }
  // A lead surrogate at the cut would be left without its pair.
  if (!out.empty() && (out.back() & 0xFC00) == 0xD800) {
    out.pop_back();
  }
  return base::UTF16ToUTF8(base::TrimWhitespace(out, base::TRIM_ALL));
}

}  // namespace

std::optional<IndexRow> CleanIndexRow(const GURL& url,
                                      std::u16string_view title,
                                      int visit_count,
                                      int typed_count,
                                      base::Time last_visit) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host() ||
      url.spec().size() > kMaxIndexUrl) {
    return std::nullopt;
  }
  IndexRow row;
  row.url = url.spec();
  row.title = CleanTitle(title);
  row.visit_count = std::max(0, visit_count);
  row.typed_count = std::max(0, typed_count);
  const int64_t at =
      last_visit.is_null() ? 0 : last_visit.InMillisecondsSinceUnixEpoch();
  row.last_visit_at = at > 0 ? static_cast<double>(at) : 0;
  return row;
}

IndexDelta::IndexDelta() = default;
IndexDelta::IndexDelta(IndexDelta&&) = default;
IndexDelta& IndexDelta::operator=(IndexDelta&&) = default;
IndexDelta::~IndexDelta() = default;

base::Value IndexDelta::ToValue() const {
  base::ListValue rows;
  for (const IndexRow& row : upserts) {
    rows.Append(base::DictValue()
                    .Set("key", row.url)
                    .Set("kind", "history")
                    .Set("url", row.url)
                    .Set("title", row.title)
                    .Set("visitCount", row.visit_count)
                    .Set("typedCount", row.typed_count)
                    .Set("lastVisitAt", row.last_visit_at));
  }
  base::ListValue gone;
  for (const std::string& key : deletes) {
    gone.Append(key);
  }
  return base::Value(base::DictValue()
                         .Set("rev", static_cast<double>(rev))
                         .Set("full", full)
                         .Set("upserts", std::move(rows))
                         .Set("deletes", std::move(gone)));
}

CluIndexSync::CluIndexSync() = default;
CluIndexSync::~CluIndexSync() = default;

// static
int64_t CluIndexSync::NextRev() {
  return ++g_rev_counter;
}

void CluIndexSync::Refresh(std::vector<IndexRow> rows) {
  std::map<std::string, IndexRow, std::less<>> next;
  for (IndexRow& row : rows) {
    if (next.size() == kIndexLimit) {
      break;
    }
    // The first (most recent) row of a URL wins.
    next.try_emplace(row.url, std::move(row));
  }
  if (!loaded_) {
    loaded_ = true;
    rows_ = std::move(next);
    rev_ = NextRev();
    floor_ = rev_;
    return;
  }
  for (const auto& [key, row] : next) {
    const auto old = rows_.find(key);
    if (old == rows_.end() || old->second != row) {
      changes_.emplace_back(NextRev(), key);
    }
  }
  for (const auto& [key, row] : rows_) {
    if (!next.contains(key)) {
      changes_.emplace_back(NextRev(), key);
    }
  }
  if (changes_.size() > kMaxIndexDelta) {
    const size_t drop = changes_.size() - kMaxIndexDelta;
    floor_ = changes_[drop - 1].first;
    changes_.erase(changes_.begin(), changes_.begin() + drop);
  }
  rows_ = std::move(next);
  if (!changes_.empty()) {
    rev_ = changes_.back().first;
  }
}

IndexDelta CluIndexSync::Sync(int64_t since_rev) const {
  IndexDelta delta;
  delta.rev = rev_;
  if (since_rev <= 0 || since_rev > rev_ || since_rev < floor_) {
    delta.full = true;
    delta.upserts.reserve(rows_.size());
    for (const auto& [key, row] : rows_) {
      delta.upserts.push_back(row);
    }
    return delta;
  }
  std::set<std::string_view> keys;
  for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) {
    if (it->first <= since_rev) {
      break;
    }
    keys.insert(it->second);
  }
  for (std::string_view key : keys) {
    const auto row = rows_.find(key);
    if (row != rows_.end()) {
      delta.upserts.push_back(row->second);
    } else {
      delta.deletes.emplace_back(key);
    }
  }
  return delta;
}

}  // namespace clu
