// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_sidebar_file.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "base/functional/function_ref.h"
#include "base/i18n/char_iterator.h"
#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "third_party/icu/source/common/unicode/uchar.h"

namespace clu {

namespace {

constexpr size_t kMaxEmojiUnits = 8;
constexpr size_t kMaxThemeWalk = 2000;

using Pair = std::pair<const base::Value*, const base::Value*>;

// A flat alternating array read in pairs; an odd tail is ignored, anything but a list reads as none.
std::vector<Pair> Pairs(const base::Value* raw) {
  std::vector<Pair> out;
  if (!raw || !raw->is_list()) {
    return out;
  }
  const base::ListValue& list = raw->GetList();
  for (size_t i = 0; i + 1 < list.size(); i += 2) {
    out.emplace_back(&list[i], &list[i + 1]);
  }
  return out;
}

const base::DictValue* Dict(const base::Value* value) {
  return value ? value->GetIfDict() : nullptr;
}

bool IsBaseName(std::string_view s) {
  if (s.empty() || s.size() > 64) {
    return false;
  }
  return std::all_of(s.begin(), s.end(), [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == '_' || c == ' ' || c == '.' ||
           c == '-';
  });
}

// `{default: {}}` is "default"; `{custom: {_0: {directoryBasename}}}` is "custom:<name>"; junk is "default".
std::string ProfileKey(const base::Value* raw) {
  const base::DictValue* custom = Dict(raw) ? Dict(raw)->FindDict("custom") : nullptr;
  const base::DictValue* inner = custom ? custom->FindDict("_0") : nullptr;
  const std::string* base = inner ? inner->FindString("directoryBasename") : nullptr;
  return base && IsBaseName(*base) ? base::StrCat({"custom:", *base})
                                   : std::string("default");
}

// The `machineID` of a `custom` profile, empty for `default` and junk. The file keeps the favorites containers of
// every machine the account synced from; only this machine's belong to the import.
std::string MachineOf(const base::Value* raw) {
  const base::DictValue* custom = Dict(raw) ? Dict(raw)->FindDict("custom") : nullptr;
  const base::DictValue* inner = custom ? custom->FindDict("_0") : nullptr;
  const std::string* machine = inner ? inner->FindString("machineID") : nullptr;
  return machine ? *machine : std::string();
}

// /\p{Extended_Pictographic}|\p{Regional_Indicator}|⃣/ and no ASCII letter.
bool LooksLikeEmoji(std::string_view value) {
  if (std::any_of(value.begin(), value.end(),
                  [](char c) { return base::IsAsciiAlpha(c); })) {
    return false;
  }
  for (base::i18n::UTF8CharIterator it(value); !it.end(); it.Advance()) {
    const int32_t c = it.get();
    if (u_hasBinaryProperty(c, UCHAR_EXTENDED_PICTOGRAPHIC) ||
        u_hasBinaryProperty(c, UCHAR_REGIONAL_INDICATOR) || c == 0x20E3) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> EmojiOf(const base::DictValue& custom_info) {
  const base::DictValue* icon = custom_info.FindDict("iconType");
  if (!icon) {
    return std::nullopt;
  }
  for (const char* key : {"emoji_v2", "emoji", "icon"}) {
    const std::string* v = icon->FindString(key);
    if (!v || v->empty() || !base::IsStringUTF8(*v)) {
      continue;
    }
    const size_t units = base::UTF8ToUTF16(*v).size();
    if (units <= kMaxEmojiUnits && LooksLikeEmoji(*v)) {
      return *v;
    }
  }
  return std::nullopt;
}

// A symbol icon ("iconType": {"icon": "planet"}) as the closest glyph Rio bundles.
std::optional<std::string> SymbolOf(const base::DictValue& custom_info) {
  const base::DictValue* icon = custom_info.FindDict("iconType");
  const std::string* v = icon ? icon->FindString("icon") : nullptr;
  if (!v || LooksLikeEmoji(*v)) {
    return std::nullopt;
  }
  return ClosestSpaceSymbol(*v);
}

double Round3(double n) {
  return std::floor(n * 1000 + 0.5) / 1000;
}

std::optional<double> Unit(const base::Value& v) {
  if (!v.is_int() && !v.is_double()) {
    return std::nullopt;
  }
  const double d = v.GetDouble();
  return std::isfinite(d) ? std::optional<double>(std::clamp(d, 0.0, 1.0))
                          : std::nullopt;
}

// `{red, green, blue}` (0..1, or 0..255 when any channel is above 1) to hue 0..360 and HSL saturation 0..1.
std::optional<ThemeColor> ColorOf(const base::DictValue& o) {
  std::array<double, 3> ch = {};
  constexpr std::array<const char*, 3> keys = {"red", "green", "blue"};
  for (size_t i = 0; i < keys.size(); ++i) {
    const base::Value* v = o.Find(keys[i]);
    if (!v || !(v->is_int() || v->is_double()) || !std::isfinite(v->GetDouble())) {
      return std::nullopt;
    }
    ch[i] = v->GetDouble();
  }
  if (const base::Value* alpha = o.Find("alpha");
      alpha && (alpha->is_int() || alpha->is_double()) && alpha->GetDouble() == 0) {
    return std::nullopt;
  }
  const double scale = (ch[0] > 1 || ch[1] > 1 || ch[2] > 1) ? 255 : 1;
  const double r = std::clamp(ch[0] / scale, 0.0, 1.0);
  const double g = std::clamp(ch[1] / scale, 0.0, 1.0);
  const double b = std::clamp(ch[2] / scale, 0.0, 1.0);
  const double max = std::max({r, g, b});
  const double min = std::min({r, g, b});
  const double d = max - min;
  const double l = (max + min) / 2;
  const double s = d == 0 ? 0 : d / (1 - std::abs(2 * l - 1));
  double h = 0;
  if (d != 0) {
    h = max == r   ? std::fmod((g - b) / d + 6, 6.0)
        : max == g ? (b - r) / d + 2
                   : (r - g) / d + 4;
  }
  ThemeColor color;
  color.h = Round3(std::min(360.0, h * 60));
  // The theme model wants h below 360.
  if (color.h >= 360) {
    color.h = 0;
  }
  color.s = Round3(std::min(1.0, s));
  return color;
}

// Depth first over dictionaries and lists, at most kMaxThemeWalk values; `visit` answering true skips below.
// Dictionary keys are visited in base::DictValue's (sorted) order.
void WalkValues(const base::Value& root,
                base::FunctionRef<bool(std::string_view, const base::Value&)> visit) {
  std::vector<std::pair<std::string, raw_ptr<const base::Value>>> stack;
  stack.emplace_back(std::string(), &root);
  size_t seen = 0;
  while (!stack.empty() && seen++ < kMaxThemeWalk) {
    auto [key, value] = std::move(stack.back());
    stack.pop_back();
    if (visit(key, *value)) {
      continue;
    }
    if (value->is_list()) {
      const base::ListValue& list = value->GetList();
      for (size_t i = list.size(); i-- > 0;) {
        stack.emplace_back(key, &list[i]);
      }
    } else if (value->is_dict()) {
      std::vector<std::pair<std::string, raw_ptr<const base::Value>>> children;
      for (const auto [k, v] : value->GetDict()) {
        children.emplace_back(k, &v);
      }
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        stack.push_back(std::move(*it));
      }
    }
  }
}

std::optional<SpaceTheme> ThemeOf(const base::Value* raw) {
  if (!Dict(raw)) {
    return std::nullopt;
  }
  SpaceTheme theme;
  auto take = [&](std::string_view, const base::Value& v) {
    if (theme.colors.size() >= kMaxThemeColors || !v.is_dict()) {
      return theme.colors.size() >= kMaxThemeColors;
    }
    std::optional<ThemeColor> c = ColorOf(v.GetDict());
    if (c) {
      theme.colors.push_back(*c);
    }
    return c.has_value();
  };
  if (const base::Value* background = Dict(raw)->Find("background");
      background && background->is_dict()) {
    WalkValues(*background, take);
  }
  if (theme.colors.empty()) {
    WalkValues(*raw, take);
  }
  if (theme.colors.empty()) {
    return std::nullopt;
  }
  std::optional<double> intensity;
  std::optional<double> grain;
  WalkValues(*raw, [&](std::string_view k, const base::Value& v) {
    if (!intensity && (k == "intensity" || k == "intensityFactor")) {
      intensity = Unit(v);
    }
    if (!grain && (k == "noise" || k == "noiseFactor" || k == "grain" ||
                   k == "grainFactor")) {
      grain = Unit(v);
    }
    return false;
  });
  theme.intensity = Round3(intensity.value_or(kDefaultThemeIntensity));
  theme.grain = Round3(grain.value_or(0));
  return theme;
}

enum class Mode { kPinned, kFavorites, kToday };

class Parser {
 public:
  Parser(const base::DictValue& main, SidebarPlan& plan) : plan_(plan) {
    for (const auto& [id, item] : Pairs(main.Find("items"))) {
      if (id->is_string() && item->is_dict()) {
        items_.emplace(id->GetString(), &item->GetDict());
      }
    }
  }

  // The nodes under a container, preorder. kPinned keeps folders; kFavorites and kToday flatten them. A split view's
  // tabs go into `splits` when it is given.
  std::vector<ImportNode> Collect(const base::Value* container_id,
                                  Mode mode,
                                  std::vector<SidebarSplitPlan>* splits = nullptr) {
    std::vector<ImportNode> out;
    const base::DictValue* container = Enter(container_id);
    if (!container) {
      return out;
    }
    struct Step {
      raw_ptr<const base::Value> id;
      std::string parent_key;
      // The split view this item is a pane of, else empty.
      std::string split_key;
    };
    std::vector<Step> stack;
    auto push = [&](const base::DictValue& item, const std::string& parent,
                    const std::string& split_key) {
      const base::ListValue* ids = item.FindList("childrenIds");
      if (!ids) {
        return;
      }
      for (size_t i = ids->size(); i-- > 0;) {
        stack.push_back({&(*ids)[i], parent, split_key});
      }
    };
    std::vector<SidebarSplitPlan> found;
    std::map<std::string, size_t, std::less<>> split_index;
    push(*container, std::string(), std::string());
    while (!stack.empty()) {
      Step step = std::move(stack.back());
      stack.pop_back();
      const base::DictValue* item = Enter(step.id);
      if (!item) {
        continue;
      }
      const base::DictValue* data = item->FindDict("data");
      const base::DictValue* tab = data ? data->FindDict("tab") : nullptr;
      const bool list = data && data->FindDict("list");
      const bool split = data && data->FindDict("splitView");
      const std::string key = base::StrCat({"item:", step.id->GetString()});
      if (tab) {
        const std::optional<std::string> url = ImportUrl(tab->Find("savedURL"));
        if (!url) {
          ++plan_->skipped.bad_urls;
          continue;
        }
        if (nodes_ >= kMaxImportItems) {
          ++plan_->skipped.over_cap;
          continue;
        }
        ++nodes_;
        ImportNode node;
        node.key = key;
        node.parent_key = step.parent_key;
        node.url = *url;
        node.title = ImportTitle(tab->Find("savedTitle"));
        if (node.title.empty()) {
          node.title = ImportHost(*url);
        }
        const std::string rename = ImportTitle(item->Find("title"));
        if (!rename.empty()) {
          node.title_override = rename;
        }
        if (const auto it = split_index.find(step.split_key);
            it != split_index.end()) {
          found[it->second].keys.push_back(node.key);
        }
        out.push_back(std::move(node));
      } else if (list && mode == Mode::kPinned) {
        if (nodes_ >= kMaxImportItems) {
          ++plan_->skipped.over_cap;
          continue;
        }
        ++nodes_;
        ImportNode folder;
        folder.key = key;
        folder.parent_key = step.parent_key;
        folder.folder = true;
        folder.title = ImportTitle(item->Find("title"));
        if (folder.title.empty()) {
          folder.title = kUntitledFolderTitle;
        }
        folder.collapsed = item->FindBool("isCollapsed").value_or(false) ||
                           item->FindBool("isExpanded") == false;
        out.push_back(std::move(folder));
        push(*item, key, std::string());
      } else if (split) {
        if (splits) {
          split_index.emplace(key, found.size());
          SidebarSplitPlan entry;
          const std::string* orientation =
              data->FindDict("splitView")->FindString("layoutOrientation");
          entry.stacked = orientation && *orientation == "vertical";
          found.push_back(std::move(entry));
        }
        push(*item, step.parent_key, key);
      } else if (list) {
        push(*item, step.parent_key, std::string());
      } else {
        ++plan_->skipped.unsupported;
      }
    }
    if (splits) {
      for (SidebarSplitPlan& entry : found) {
        if (entry.keys.size() >= 2) {
          splits->push_back(std::move(entry));
        }
      }
    }
    return out;
  }

 private:
  // The item for an id, counting a missing one; null when it was already visited (a cycle).
  const base::DictValue* Enter(const base::Value* id) {
    const auto it = id && id->is_string() ? items_.find(id->GetString())
                                          : items_.end();
    if (it == items_.end()) {
      ++plan_->skipped.missing;
      return nullptr;
    }
    if (!visited_.insert(it->first).second) {
      ++plan_->skipped.cycles;
      return nullptr;
    }
    return it->second;
  }

  const raw_ref<SidebarPlan> plan_;
  std::map<std::string, raw_ptr<const base::DictValue>, std::less<>> items_;
  std::set<std::string, std::less<>> visited_;
  size_t nodes_ = 0;
};

}  // namespace

SidebarSplitPlan::SidebarSplitPlan() = default;
SidebarSplitPlan::SidebarSplitPlan(const SidebarSplitPlan&) = default;
SidebarSplitPlan& SidebarSplitPlan::operator=(const SidebarSplitPlan&) = default;
SidebarSplitPlan::SidebarSplitPlan(SidebarSplitPlan&&) = default;
SidebarSplitPlan& SidebarSplitPlan::operator=(SidebarSplitPlan&&) = default;
SidebarSplitPlan::~SidebarSplitPlan() = default;

SidebarSpacePlan::SidebarSpacePlan() = default;
SidebarSpacePlan::SidebarSpacePlan(const SidebarSpacePlan&) = default;
SidebarSpacePlan& SidebarSpacePlan::operator=(const SidebarSpacePlan&) = default;
SidebarSpacePlan::SidebarSpacePlan(SidebarSpacePlan&&) = default;
SidebarSpacePlan& SidebarSpacePlan::operator=(SidebarSpacePlan&&) = default;
SidebarSpacePlan::~SidebarSpacePlan() = default;

SidebarProfilePlan::SidebarProfilePlan() = default;
SidebarProfilePlan::SidebarProfilePlan(const SidebarProfilePlan&) = default;
SidebarProfilePlan& SidebarProfilePlan::operator=(const SidebarProfilePlan&) =
    default;
SidebarProfilePlan::SidebarProfilePlan(SidebarProfilePlan&&) = default;
SidebarProfilePlan& SidebarProfilePlan::operator=(SidebarProfilePlan&&) =
    default;
SidebarProfilePlan::~SidebarProfilePlan() = default;

SidebarPlan::SidebarPlan() = default;
SidebarPlan::SidebarPlan(SidebarPlan&&) = default;
SidebarPlan& SidebarPlan::operator=(SidebarPlan&&) = default;
SidebarPlan::~SidebarPlan() = default;

SidebarPlan ParseSidebarFile(std::string_view text) {
  SidebarPlan plan;
  std::optional<base::Value> data = base::JSONReader::Read(
      text, base::JSON_PARSE_RFC, kMaxImportJsonDepth);
  const base::DictValue* sidebar =
      data && data->is_dict() ? data->GetDict().FindDict("sidebar") : nullptr;
  const base::ListValue* containers =
      sidebar ? sidebar->FindList("containers") : nullptr;
  const base::DictValue* main =
      containers && containers->size() > 1 ? (*containers)[1].GetIfDict()
                                            : nullptr;
  if (!main || !main->FindList("spaces")) {
    plan.error = kSidebarUnsupported;
    return plan;
  }

  Parser parser(*main, plan);
  const base::DictValue no_info;
  std::map<std::string, size_t> profile_index;
  auto profile_for = [&](const std::string& key) -> SidebarProfilePlan& {
    auto [it, added] = profile_index.emplace(key, plan.profiles.size());
    if (added) {
      SidebarProfilePlan profile;
      profile.key = key;
      plan.profiles.push_back(std::move(profile));
    }
    return plan.profiles[it->second];
  };

  // This machine is the one the spaces' profiles name.
  std::string machine;
  for (const auto& [id, raw] : Pairs(main->Find("spaces"))) {
    if (!id->is_string() || !raw->is_dict()) {
      ++plan.skipped.unsupported;
      continue;
    }
    const base::DictValue& space = raw->GetDict();
    std::map<std::string, const base::Value*> container_ids;
    for (const auto& [kind, cid] : Pairs(space.Find("containerIDs"))) {
      if (kind->is_string()) {
        container_ids.emplace(kind->GetString(), cid);
      }
    }
    SidebarSpacePlan entry;
    entry.key = base::StrCat({"space:", id->GetString()});
    entry.profile_key = profile_for(ProfileKey(space.Find("profile"))).key;
    if (machine.empty()) {
      machine = MachineOf(space.Find("profile"));
    }
    if (const auto it = container_ids.find("pinned"); it != container_ids.end()) {
      entry.pinned = parser.Collect(it->second, Mode::kPinned, &entry.splits);
    }
    if (const auto it = container_ids.find("unpinned");
        it != container_ids.end()) {
      entry.today = parser.Collect(it->second, Mode::kToday, &entry.splits);
    }
    const base::DictValue* custom_info = space.FindDict("customInfo");
    const base::DictValue& info = custom_info ? *custom_info : no_info;
    entry.name = ImportTitle(space.Find("title"));
    entry.emoji = EmojiOf(info);
    if (!entry.emoji) {
      entry.symbol = SymbolOf(info);
    }
    entry.theme = ThemeOf(info.Find("windowTheme"));
    plan.spaces.push_back(std::move(entry));
  }
  // Favorites only for a profile a space uses, and only this machine's container: profile folders no space uses and
  // other machines' copies stay behind (10 folders on disk, 4 spaces, 4 profiles).
  for (const auto& [profile, cid] : Pairs(main->Find("topAppsContainerIDs"))) {
    const auto it = profile_index.find(ProfileKey(profile));
    const std::string other = MachineOf(profile);
    if (it == profile_index.end() ||
        (!machine.empty() && !other.empty() && other != machine)) {
      continue;
    }
    std::vector<ImportNode> favorites = parser.Collect(cid, Mode::kFavorites);
    SidebarProfilePlan& p = plan.profiles[it->second];
    for (ImportNode& node : favorites) {
      p.favorites.push_back(std::move(node));
    }
  }
  return plan;
}

}  // namespace clu
