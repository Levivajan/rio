// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/multitask/clu_split_rules.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "clu/browser/ui/webui/bridge/validation.h"

namespace clu {

namespace {

// Depth of a stored tree: a root group is 1, a group inside it 2 (MAX_DEPTH).
constexpr int kMaxLayoutDepth = 2;
// MAX_PANES in src/shared/split.ts: what a stored layout may hold, whatever
// this build shows.
constexpr size_t kMaxStoredPanes = 4;

bool OnlyKeys(const base::DictValue& dict,
              std::initializer_list<std::string_view> keys) {
  for (const auto [key, value] : dict) {
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
      return false;
    }
  }
  return true;
}

// Walks one node of a stored tree. Panes go to `out` in tree order. False for
// junk.
bool WalkNode(const base::Value& value,
              int depth,
              std::vector<std::string>& out,
              ParsedSplitLayout* root_info) {
  const base::DictValue* dict = value.GetIfDict();
  if (!dict) {
    return false;
  }
  const std::string* kind = dict->FindString("kind");
  if (!kind) {
    return false;
  }
  if (*kind == "pane") {
    const std::string* tab_id = dict->FindString("tabId");
    if (depth == 0 || !tab_id || !IsUuidV4(*tab_id) ||
        !OnlyKeys(*dict, {"kind", "tabId"})) {
      return false;
    }
    out.push_back(*tab_id);
    return true;
  }
  if (*kind != "group" || depth >= kMaxLayoutDepth ||
      !OnlyKeys(*dict, {"kind", "dir", "sizes", "children"})) {
    return false;
  }
  const std::string* dir = dict->FindString("dir");
  const base::ListValue* sizes = dict->FindList("sizes");
  const base::ListValue* children = dict->FindList("children");
  if (!dir || (*dir != "row" && *dir != "col") || !sizes || !children ||
      children->size() < 2 || children->size() > kMaxStoredPanes ||
      sizes->size() != children->size()) {
    return false;
  }
  std::vector<double> numbers;
  for (const base::Value& size : *sizes) {
    if (!size.is_double() && !size.is_int()) {
      return false;
    }
    const double v = size.GetDouble();
    if (!std::isfinite(v) || v <= 0 || v >= 1) {
      return false;
    }
    numbers.push_back(v);
  }
  if (!SizesSumToOne(numbers)) {
    return false;
  }
  if (root_info) {
    root_info->dir = *dir == "row" ? SplitDir::kRow : SplitDir::kCol;
    const bool flat =
        std::all_of(
            children->begin(), children->end(), [](const base::Value& c) {
              const base::DictValue* d = c.GetIfDict();
              const std::string* k = d ? d->FindString("kind") : nullptr;
              return k && *k == "pane";
            });
    if (flat && children->size() == 2) {
      root_info->ratio = numbers[0];
    } else if (flat) {
      root_info->sizes = numbers;
    }
  }
  for (const base::Value& child : *children) {
    if (!WalkNode(child, depth + 1, out, nullptr)) {
      return false;
    }
  }
  return true;
}

double SaneRatio(double ratio) {
  if (!std::isfinite(ratio)) {
    return 0.5;
  }
  return std::clamp(ratio, kMinSplitRatio, 1.0 - kMinSplitRatio);
}

}  // namespace

std::string_view SplitDirName(SplitDir dir) {
  return dir == SplitDir::kRow ? "row" : "col";
}

SplitRow::SplitRow() = default;
SplitRow::SplitRow(const SplitRow&) = default;
SplitRow& SplitRow::operator=(const SplitRow&) = default;
SplitRow::SplitRow(SplitRow&&) = default;
SplitRow& SplitRow::operator=(SplitRow&&) = default;
SplitRow::~SplitRow() = default;

RawSplitRow::RawSplitRow() = default;
RawSplitRow::RawSplitRow(const RawSplitRow&) = default;
RawSplitRow& RawSplitRow::operator=(const RawSplitRow&) = default;
RawSplitRow::~RawSplitRow() = default;

ParsedSplitLayout::ParsedSplitLayout() = default;
ParsedSplitLayout::ParsedSplitLayout(const ParsedSplitLayout&) = default;
ParsedSplitLayout& ParsedSplitLayout::operator=(const ParsedSplitLayout&) =
    default;
ParsedSplitLayout::~ParsedSplitLayout() = default;

SplitRepairResult::SplitRepairResult() = default;
SplitRepairResult::SplitRepairResult(SplitRepairResult&&) = default;
SplitRepairResult& SplitRepairResult::operator=(SplitRepairResult&&) = default;
SplitRepairResult::~SplitRepairResult() = default;

std::optional<ParsedSplitLayout> ParseSplitLayout(std::string_view json) {
  if (json.empty() || json.size() > kMaxSplitLayoutBytes) {
    return std::nullopt;
  }
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!value) {
    return std::nullopt;
  }
  ParsedSplitLayout parsed;
  // The root is a group (a lone pane is no split).
  const base::DictValue* dict = value->GetIfDict();
  const std::string* kind = dict ? dict->FindString("kind") : nullptr;
  if (!kind || *kind != "group" ||
      !WalkNode(*value, 0, parsed.tab_ids, &parsed)) {
    return std::nullopt;
  }
  if (parsed.tab_ids.size() < 2 || parsed.tab_ids.size() > kMaxStoredPanes ||
      !UniqueIds(parsed.tab_ids)) {
    return std::nullopt;
  }
  return parsed;
}

base::DictValue SplitRootValue(const SplitRow& split) {
  base::ListValue children;
  base::ListValue sizes;
  const double ratio = SaneRatio(split.ratio);
  for (size_t i = 0; i < split.tab_ids.size(); ++i) {
    children.Append(
        base::DictValue().Set("kind", "pane").Set("tabId", split.tab_ids[i]));
  }
  if (split.tab_ids.size() == 2) {
    sizes.Append(ratio);
    sizes.Append(1.0 - ratio);
  } else {
    for (double share : PaneShares(split.sizes, split.tab_ids.size())) {
      sizes.Append(share);
    }
  }
  return base::DictValue()
      .Set("kind", "group")
      .Set("dir", SplitDirName(split.dir))
      .Set("sizes", std::move(sizes))
      .Set("children", std::move(children));
}

std::string SplitLayoutJson(const SplitRow& split) {
  std::string json;
  base::JSONWriter::Write(base::Value(SplitRootValue(split)), &json);
  return json;
}

SplitRepairResult RepairSplits(const std::vector<RawSplitRow>& rows,
                               const std::vector<SplitMember>& members) {
  SplitRepairResult result;
  std::map<std::string, const SplitMember*> by_id;
  for (const SplitMember& member : members) {
    by_id.emplace(member.id, &member);
  }
  // Which split each tab ends up in.
  std::map<std::string, std::string> owner;
  std::set<std::string> seen_ids;

  for (const RawSplitRow& raw : rows) {
    if (!raw.id || seen_ids.contains(*raw.id)) {
      continue;
    }
    if (!IsUuidV4(*raw.id)) {
      result.dropped.push_back(*raw.id);
      continue;
    }
    seen_ids.insert(*raw.id);
    SplitRow split;
    split.id = *raw.id;
    split.created_at = raw.created_at;
    split.updated_at = raw.updated_at;

    std::optional<ParsedSplitLayout> parsed =
        raw.layout ? ParseSplitLayout(*raw.layout) : std::nullopt;
    bool rewrite = !parsed;
    std::vector<std::string> candidates;
    if (parsed) {
      candidates = parsed->tab_ids;
      split.dir = parsed->dir;
      split.ratio = parsed->ratio.value_or(0.5);
      split.sizes = parsed->sizes;
    } else {
      // Rebuilt as a flat row from the tabs carrying this split id, in sidebar
      // order.
      for (const SplitMember& member : members) {
        if (member.split_id == split.id) {
          candidates.push_back(member.id);
        }
      }
    }

    // The split's space is its first real pane's; panes elsewhere, favorites
    // and tabs taken leave it.
    std::string space_id;
    for (const std::string& id : candidates) {
      const auto it = by_id.find(id);
      if (it == by_id.end() || it->second->favorite ||
          it->second->space_id.empty() || owner.contains(id)) {
        rewrite = true;
        continue;
      }
      if (space_id.empty()) {
        space_id = it->second->space_id;
      }
      if (it->second->space_id != space_id) {
        rewrite = true;
        continue;
      }
      if (split.tab_ids.size() >= kMaxSplitPanes) {
        rewrite = true;
        continue;
      }
      split.tab_ids.push_back(id);
    }
    if (split.tab_ids.size() < 2) {
      result.dropped.push_back(split.id);
      continue;
    }
    split.space_id = space_id;
    if (raw.space_id != space_id) {
      rewrite = true;
    }
    // Shares stored for other panes than the ones kept are even again.
    if (!split.sizes.empty() && split.sizes.size() != split.tab_ids.size()) {
      split.sizes.clear();
      rewrite = true;
    }
    const double sane = SaneRatio(split.ratio);
    if (sane != split.ratio) {
      rewrite = true;
    }
    split.ratio = sane;
    if (raw.focused_tab_id &&
        std::find(split.tab_ids.begin(), split.tab_ids.end(),
                  *raw.focused_tab_id) != split.tab_ids.end()) {
      split.focused_tab_id = *raw.focused_tab_id;
    } else {
      split.focused_tab_id = split.tab_ids.front();
      rewrite = true;
    }
    for (const std::string& id : split.tab_ids) {
      owner[id] = split.id;
    }
    if (rewrite) {
      result.rewritten.push_back(split.id);
    }
    result.splits.push_back(std::move(split));
  }

  // Every tab row carries exactly the split that holds it.
  for (const SplitMember& member : members) {
    const auto it = owner.find(member.id);
    std::optional<std::string> want;
    if (it != owner.end()) {
      want = it->second;
    }
    if (member.split_id != want) {
      result.member_fixes.emplace_back(member.id, want);
    }
  }
  return result;
}

DropPlacement PlaceDrop(SplitEdge edge) {
  switch (edge) {
    case SplitEdge::kLeft:
      return {SplitDir::kRow, true};
    case SplitEdge::kRight:
      return {SplitDir::kRow, false};
    case SplitEdge::kTop:
      return {SplitDir::kCol, true};
    case SplitEdge::kBottom:
      return {SplitDir::kCol, false};
  }
  return {};
}

double ClampSplitRatio(double ratio, int avail, int min_pane, double current) {
  if (!std::isfinite(ratio) || ratio <= 0 || ratio >= 1) {
    return SaneRatio(current);
  }
  if (avail <= 0 || min_pane <= 0) {
    return SaneRatio(ratio);
  }
  if (avail < 2 * min_pane) {
    return 0.5;
  }
  const double min_share = static_cast<double>(min_pane) / avail;
  return SaneRatio(std::clamp(ratio, min_share, 1.0 - min_share));
}

bool UniqueIds(base::span<const std::string> ids) {
  std::set<std::string_view> seen;
  for (const std::string& id : ids) {
    if (!seen.insert(id).second) {
      return false;
    }
  }
  return true;
}

bool SizesSumToOne(base::span<const double> sizes) {
  double sum = 0;
  for (double size : sizes) {
    if (!std::isfinite(size)) {
      return false;
    }
    sum += size;
  }
  return std::abs(sum - 1.0) <= kSplitSizeTolerance;
}

std::vector<double> PaneShares(base::span<const double> sizes, size_t count) {
  bool valid = count > 0 && sizes.size() == count && SizesSumToOne(sizes);
  for (double size : sizes) {
    valid = valid && size > 0 && size < 1;
  }
  if (valid) {
    return std::vector<double>(sizes.begin(), sizes.end());
  }
  return std::vector<double>(count, count ? 1.0 / static_cast<double>(count)
                                          : 0.0);
}

std::vector<double> ExpandedShares(size_t count,
                                   size_t index,
                                   int room,
                                   int min_pane) {
  if (count < 2 || index >= count || room <= 0 || min_pane < 0) {
    return {};
  }
  const double other = (min_pane + 1.0) / room;
  const double expanded = 1.0 - other * static_cast<double>(count - 1);
  if (expanded <= other) {
    return {};
  }
  std::vector<double> shares(count, other);
  shares[index] = expanded;
  return shares;
}

std::optional<std::string> PaneAt(base::span<const std::string> panes,
                                  int index) {
  if (index < 1 || static_cast<size_t>(index) > panes.size()) {
    return std::nullopt;
  }
  return panes[index - 1];
}

std::optional<std::string> StepPane(base::span<const std::string> panes,
                                    std::string_view current,
                                    int step) {
  if (panes.empty() || (step != 1 && step != -1)) {
    return std::nullopt;
  }
  const auto it = std::find(panes.begin(), panes.end(), current);
  if (it == panes.end()) {
    return panes.front();
  }
  const int n = static_cast<int>(panes.size());
  const int i = static_cast<int>(it - panes.begin());
  return panes[(i + step + n) % n];
}

}  // namespace clu
