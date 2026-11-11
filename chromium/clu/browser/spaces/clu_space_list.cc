// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/spaces/clu_space_list.h"

#include <algorithm>
#include <utility>

#include "base/strings/string_util.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/ui/webui/bridge/validation.h"

namespace clu {

namespace {

constexpr size_t kMaxDirLength = 255;

constexpr char kKeyId[] = "id";
constexpr char kKeyDir[] = "dir";
constexpr char kKeyName[] = "name";
constexpr char kKeyDefault[] = "default";
constexpr char kKeyIconKind[] = "icon_kind";
constexpr char kKeyIconValue[] = "icon_value";
constexpr char kKeyColor[] = "color";
constexpr char kKeyProfileId[] = "profile_id";
constexpr char kKeyLostProfileId[] = "lost_profile_id";

std::string StringOr(const base::DictValue& dict, std::string_view key) {
  const std::string* value = dict.FindString(key);
  return value ? *value : std::string();
}

// The icon a stored or requested value stands for: one emoji or a known symbol, else the letter icon.
void SetIcon(SpaceEntry& space, const SpaceIconInput& icon) {
  space.icon_kind = "letter";
  space.icon_value.clear();
  if (icon.emoji) {
    if (std::optional<std::string> value = ParseEmoji(icon.value)) {
      space.icon_kind = "emoji";
      space.icon_value = std::move(*value);
    }
  } else if (icon.symbol && IsSpaceSymbol(icon.value)) {
    space.icon_kind = "symbol";
    space.icon_value = icon.value;
  }
}

// A requested icon SetIcon() would not keep as asked.
bool IsBadIcon(const SpaceIconInput& icon) {
  return (icon.emoji && !ParseEmoji(icon.value)) ||
         (icon.symbol && !IsSpaceSymbol(icon.value));
}

SpaceIconInput StoredIcon(std::string_view kind, std::string_view value) {
  return {.emoji = kind == "emoji",
          .value = std::string(value),
          .symbol = kind == "symbol"};
}

}  // namespace

ProfileEntry::ProfileEntry() = default;
ProfileEntry::ProfileEntry(const ProfileEntry&) = default;
ProfileEntry& ProfileEntry::operator=(const ProfileEntry&) = default;
ProfileEntry::~ProfileEntry() = default;

SpaceEntry::SpaceEntry() = default;
SpaceEntry::SpaceEntry(const SpaceEntry&) = default;
SpaceEntry& SpaceEntry::operator=(const SpaceEntry&) = default;
SpaceEntry::~SpaceEntry() = default;

std::string_view SpacesErrorReason(SpacesError error) {
  switch (error) {
    case SpacesError::kNotFound:
      return "not-found";
    case SpacesError::kBadPayload:
      return "bad-payload";
    case SpacesError::kMaxSpaces:
      return "max-spaces";
    case SpacesError::kMaxProfiles:
      return "max-profiles";
    case SpacesError::kLastSpace:
      return "last-space";
    case SpacesError::kDefaultProfile:
      return "default-profile";
    case SpacesError::kInUse:
      return "in-use";
  }
  return "rejected";
}

CluSpaceList::CluSpaceList(IdSource new_id) : new_id_(std::move(new_id)) {}

CluSpaceList::~CluSpaceList() = default;

// static
bool CluSpaceList::IsSafeDir(std::string_view dir) {
  if (dir.empty() || dir.size() > kMaxDirLength || dir == "." || dir == ".." ||
      dir.front() == '.' || dir.front() == ' ' || dir.back() == ' ') {
    return false;
  }
  return std::ranges::all_of(dir, [](char c) {
    return base::IsAsciiAlphaNumeric(c) || c == ' ' || c == '_' || c == '-' ||
           c == '.';
  });
}

std::set<SpacesFix> CluSpaceList::Load(const base::ListValue* profiles,
                                       const base::ListValue* spaces,
                                       std::string_view last_space) {
  std::set<SpacesFix> fixes;
  profiles_.clear();
  spaces_.clear();
  last_space_.clear();

  // Profiles: unreadable rows go; names renormalized; exactly one default.
  if (profiles) {
    for (const base::Value& value : *profiles) {
      const base::DictValue* dict = value.GetIfDict();
      const std::string id = dict ? StringOr(*dict, kKeyId) : std::string();
      const std::string dir = dict ? StringOr(*dict, kKeyDir) : std::string();
      if (!dict || !IsUuidV4(id) || !IsSafeDir(dir) || FindProfile(id) ||
          FindProfileByDir(dir)) {
        fixes.insert(SpacesFix::kProfileJunk);
        continue;
      }
      ProfileEntry entry;
      entry.id = id;
      entry.dir = dir;
      const std::string raw_name = StringOr(*dict, kKeyName);
      std::optional<std::string> name = NormalizeName(raw_name);
      if (!name) {
        name = NextDefaultName("Profile", static_cast<int>(profiles_.size()) + 1,
                               ProfileNames());
      }
      if (*name != raw_name) {
        fixes.insert(SpacesFix::kProfileName);
      }
      entry.name = std::move(*name);
      entry.is_default = dict->FindBool(kKeyDefault).value_or(false);
      profiles_.push_back(std::move(entry));
    }
  }
  bool has_default = false;
  for (ProfileEntry& profile : profiles_) {
    if (profile.is_default && has_default) {
      profile.is_default = false;
      fixes.insert(SpacesFix::kDefaultDuplicate);
    }
    has_default = has_default || profile.is_default;
  }
  if (!has_default && !profiles_.empty()) {
    // Profiles exist but none is the default: the first becomes it, so spaces keep their profiles.
    profiles_.front().is_default = true;
    fixes.insert(SpacesFix::kDefaultPromoted);
  }
  const ProfileEntry* default_profile = DefaultProfile();

  // Spaces, in stored order.
  if (spaces) {
    for (const base::Value& value : *spaces) {
      const base::DictValue* dict = value.GetIfDict();
      const std::string id = dict ? StringOr(*dict, kKeyId) : std::string();
      if (!dict || !IsUuidV4(id) || FindSpace(id)) {
        fixes.insert(SpacesFix::kSpaceJunk);
        continue;
      }
      SpaceEntry space;
      space.id = id;
      space.profile_id = StringOr(*dict, kKeyProfileId);
      const std::string lost = StringOr(*dict, kKeyLostProfileId);
      if (IsUuidV4(lost) && !FindProfile(lost)) {
        space.lost_profile_id = lost;
      }
      if (!FindProfile(space.profile_id)) {
        // The profile row was junk: the id is kept so the profile's adoption brings the space back to its rows.
        if (IsUuidV4(space.profile_id)) {
          space.lost_profile_id = space.profile_id;
        }
        // With no profile at all the space waits for the first profile Clu adopts (AdoptProfile()).
        space.profile_id = default_profile ? default_profile->id : std::string();
        fixes.insert(SpacesFix::kSpaceProfile);
      }
      const std::string color = StringOr(*dict, kKeyColor);
      if (IsSpaceColor(color)) {
        space.color = color;
      } else {
        space.color = "slate";
        fixes.insert(SpacesFix::kSpaceColor);
      }
      const std::string icon_kind = StringOr(*dict, kKeyIconKind);
      const std::string icon_value = StringOr(*dict, kKeyIconValue);
      SetIcon(space, StoredIcon(icon_kind, icon_value));
      if (space.icon_kind != icon_kind || space.icon_value != icon_value) {
        fixes.insert(SpacesFix::kSpaceIcon);
      }
      const std::string raw_name = StringOr(*dict, kKeyName);
      std::optional<std::string> name = NormalizeName(raw_name);
      if (!name) {
        name = NextDefaultName("Space", static_cast<int>(spaces_.size()) + 1,
                               SpaceNames());
      }
      if (*name != raw_name) {
        fixes.insert(SpacesFix::kSpaceName);
      }
      space.name = std::move(*name);
      spaces_.push_back(std::move(space));
    }
  }

  if (!last_space.empty()) {
    if (FindSpace(last_space)) {
      last_space_ = std::string(last_space);
    } else {
      fixes.insert(SpacesFix::kLastSpace);
    }
  }
  return fixes;
}

base::ListValue CluSpaceList::ProfilesValue() const {
  base::ListValue list;
  for (const ProfileEntry& profile : profiles_) {
    list.Append(base::DictValue()
                    .Set(kKeyId, profile.id)
                    .Set(kKeyDir, profile.dir)
                    .Set(kKeyName, profile.name)
                    .Set(kKeyDefault, profile.is_default));
  }
  return list;
}

base::ListValue CluSpaceList::SpacesValue() const {
  base::ListValue list;
  for (const SpaceEntry& space : spaces_) {
    list.Append(base::DictValue()
                    .Set(kKeyId, space.id)
                    .Set(kKeyName, space.name)
                    .Set(kKeyIconKind, space.icon_kind)
                    .Set(kKeyIconValue, space.icon_value)
                    .Set(kKeyColor, space.color)
                    .Set(kKeyProfileId, space.profile_id));
    if (!space.lost_profile_id.empty()) {
      list.back().GetDict().Set(kKeyLostProfileId, space.lost_profile_id);
    }
  }
  return list;
}

std::vector<std::string> CluSpaceList::Order() const {
  std::vector<std::string> order;
  for (const SpaceEntry& space : spaces_) {
    order.push_back(space.id);
  }
  return order;
}

void CluSpaceList::SetLastSpace(std::string_view space_id) {
  if (FindSpace(space_id)) {
    last_space_ = std::string(space_id);
  }
}

const ProfileEntry* CluSpaceList::FindProfile(std::string_view id) const {
  for (const ProfileEntry& profile : profiles_) {
    if (!id.empty() && profile.id == id) {
      return &profile;
    }
  }
  return nullptr;
}

const ProfileEntry* CluSpaceList::FindProfileByDir(std::string_view dir) const {
  for (const ProfileEntry& profile : profiles_) {
    if (!dir.empty() && profile.dir == dir) {
      return &profile;
    }
  }
  return nullptr;
}

const ProfileEntry* CluSpaceList::DefaultProfile() const {
  for (const ProfileEntry& profile : profiles_) {
    if (profile.is_default) {
      return &profile;
    }
  }
  return profiles_.empty() ? nullptr : &profiles_.front();
}

const SpaceEntry* CluSpaceList::FindSpace(std::string_view id) const {
  for (const SpaceEntry& space : spaces_) {
    if (!id.empty() && space.id == id) {
      return &space;
    }
  }
  return nullptr;
}

SpaceEntry* CluSpaceList::MutableSpace(std::string_view id) {
  return const_cast<SpaceEntry*>(std::as_const(*this).FindSpace(id));
}

std::vector<SpaceEntry> CluSpaceList::SpacesOf(
    std::string_view profile_id) const {
  std::vector<SpaceEntry> out;
  for (const SpaceEntry& space : spaces_) {
    if (space.profile_id == profile_id) {
      out.push_back(space);
    }
  }
  return out;
}

int CluSpaceList::IndexOf(std::string_view space_id) const {
  for (size_t i = 0; i < spaces_.size(); ++i) {
    if (spaces_[i].id == space_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::vector<std::string> CluSpaceList::SpaceNames() const {
  std::vector<std::string> names;
  for (const SpaceEntry& space : spaces_) {
    names.push_back(space.name);
  }
  return names;
}

std::vector<std::string> CluSpaceList::ProfileNames() const {
  std::vector<std::string> names;
  for (const ProfileEntry& profile : profiles_) {
    names.push_back(profile.name);
  }
  return names;
}

std::string CluSpaceList::AdoptProfile(std::string id,
                                       std::string dir,
                                       std::vector<SpaceEntry> spaces) {
  if (const ProfileEntry* known = FindProfileByDir(dir)) {
    return known->id;
  }
  ProfileEntry entry;
  entry.id = IsUuidV4(id) && !FindProfile(id) ? std::move(id) : new_id_.Run();
  entry.dir = std::move(dir);
  const bool first = profiles_.empty();
  entry.is_default = first;
  entry.name = first ? std::string(kDefaultProfileName)
                     : NextDefaultName("Profile",
                                       static_cast<int>(profiles_.size()) + 1,
                                       ProfileNames());
  profiles_.push_back(entry);
  // Spaces that lost their profile to a repair before any profile existed belong to the first one; spaces that lost
  // this very profile to a repair go back to it, where their rows are.
  for (SpaceEntry& space : spaces_) {
    if ((first && space.profile_id.empty()) ||
        space.lost_profile_id == entry.id) {
      space.profile_id = entry.id;
      space.lost_profile_id.clear();
    }
  }
  for (SpaceEntry& space : spaces) {
    if (!IsUuidV4(space.id) || FindSpace(space.id)) {
      continue;
    }
    space.profile_id = entry.id;
    if (!IsSpaceColor(space.color)) {
      space.color = "slate";
    }
    SetIcon(space, StoredIcon(space.icon_kind, space.icon_value));
    std::optional<std::string> name = NormalizeName(space.name);
    space.name = name ? std::move(*name)
                      : NextDefaultName("Space",
                                        static_cast<int>(spaces_.size()) + 1,
                                        SpaceNames());
    spaces_.push_back(std::move(space));
  }
  return entry.id;
}

base::expected<std::string, SpacesError> CluSpaceList::CreateSpace(
    const std::optional<std::string>& name,
    const std::optional<SpaceIconInput>& icon,
    const std::optional<std::string>& color,
    const std::optional<std::string>& profile_id) {
  if (spaces_.size() >= kMaxSpaces) {
    return base::unexpected(SpacesError::kMaxSpaces);
  }
  const ProfileEntry* profile =
      profile_id ? FindProfile(*profile_id) : DefaultProfile();
  if (!profile) {
    return base::unexpected(SpacesError::kNotFound);
  }
  SpaceEntry space;
  space.color = kNewSpaceColor;
  if (icon) {
    if (IsBadIcon(*icon)) {
      return base::unexpected(SpacesError::kBadPayload);
    }
    SetIcon(space, *icon);
  }
  if (color) {
    if (!IsSpaceColor(*color)) {
      return base::unexpected(SpacesError::kBadPayload);
    }
    space.color = *color;
  }
  std::optional<std::string> clean = name ? NormalizeName(*name) : std::nullopt;
  space.name = clean ? std::move(*clean)
                     : NextDefaultName("Space",
                                       static_cast<int>(spaces_.size()) + 1,
                                       SpaceNames());
  space.id = new_id_.Run();
  space.profile_id = profile->id;
  spaces_.push_back(space);
  return space.id;
}

base::expected<bool, SpacesError> CluSpaceList::UpdateSpace(
    std::string_view space_id,
    const std::optional<std::string>& name,
    const std::optional<SpaceIconInput>& icon,
    const std::optional<std::string>& color) {
  SpaceEntry* space = MutableSpace(space_id);
  if (!space) {
    return base::unexpected(SpacesError::kNotFound);
  }
  SpaceEntry next = *space;
  if (name) {
    std::optional<std::string> clean = NormalizeName(*name);
    if (!clean) {
      return base::unexpected(SpacesError::kBadPayload);
    }
    next.name = std::move(*clean);
  }
  if (icon) {
    if (IsBadIcon(*icon)) {
      return base::unexpected(SpacesError::kBadPayload);
    }
    SetIcon(next, *icon);
  }
  if (color) {
    if (!IsSpaceColor(*color)) {
      return base::unexpected(SpacesError::kBadPayload);
    }
    next.color = *color;
  }
  if (next.name == space->name && next.icon_kind == space->icon_kind &&
      next.icon_value == space->icon_value && next.color == space->color) {
    return false;
  }
  *space = std::move(next);
  return true;
}

base::expected<bool, SpacesError> CluSpaceList::ReorderSpace(
    std::string_view space_id,
    int to_index) {
  if (!FindSpace(space_id)) {
    return base::unexpected(SpacesError::kNotFound);
  }
  std::optional<std::vector<std::string>> order =
      ReorderSpaces(Order(), space_id, to_index);
  if (!order) {
    return false;
  }
  std::vector<SpaceEntry> next;
  for (const std::string& id : *order) {
    next.push_back(*FindSpace(id));
  }
  spaces_ = std::move(next);
  return true;
}

base::expected<std::string, SpacesError> CluSpaceList::DeleteSpace(
    std::string_view space_id) {
  if (!FindSpace(space_id)) {
    return base::unexpected(SpacesError::kNotFound);
  }
  if (spaces_.size() <= 1) {
    return base::unexpected(SpacesError::kLastSpace);
  }
  // A copy: `space_id` may point into the entry being erased.
  const std::string id(space_id);
  std::optional<std::string> fallback = FallbackSpace(Order(), id);
  std::erase_if(spaces_,
                [&](const SpaceEntry& space) { return space.id == id; });
  if (last_space_ == id) {
    last_space_ = fallback.value_or(std::string());
  }
  return fallback.value_or(spaces_.front().id);
}

base::expected<bool, SpacesError> CluSpaceList::SetSpaceProfile(
    std::string_view space_id,
    std::string_view profile_id) {
  SpaceEntry* space = MutableSpace(space_id);
  if (!space || !FindProfile(profile_id)) {
    return base::unexpected(SpacesError::kNotFound);
  }
  if (space->profile_id == profile_id) {
    return false;
  }
  space->profile_id = std::string(profile_id);
  // The user chose its profile: a lost one no longer claims it.
  space->lost_profile_id.clear();
  return true;
}

base::expected<std::string, SpacesError> CluSpaceList::AddProfile(
    const std::optional<std::string>& name,
    std::string dir) {
  if (profiles_.size() >= kMaxProfiles) {
    return base::unexpected(SpacesError::kMaxProfiles);
  }
  if (!IsSafeDir(dir) || FindProfileByDir(dir)) {
    return base::unexpected(SpacesError::kBadPayload);
  }
  ProfileEntry entry;
  entry.id = new_id_.Run();
  entry.dir = std::move(dir);
  std::optional<std::string> clean = name ? NormalizeName(*name) : std::nullopt;
  entry.name = clean ? std::move(*clean)
                     : NextDefaultName("Profile",
                                       static_cast<int>(profiles_.size()) + 1,
                                       ProfileNames());
  entry.is_default = profiles_.empty();
  profiles_.push_back(entry);
  return entry.id;
}

base::expected<bool, SpacesError> CluSpaceList::RenameProfile(
    std::string_view profile_id,
    std::string_view name) {
  const ProfileEntry* profile = FindProfile(profile_id);
  if (!profile) {
    return base::unexpected(SpacesError::kNotFound);
  }
  std::optional<std::string> clean = NormalizeName(name);
  if (!clean) {
    return base::unexpected(SpacesError::kBadPayload);
  }
  if (*clean == profile->name) {
    return false;
  }
  const_cast<ProfileEntry*>(profile)->name = std::move(*clean);
  return true;
}

base::expected<void, SpacesError> CluSpaceList::CanRemoveProfile(
    std::string_view profile_id) const {
  const ProfileEntry* profile = FindProfile(profile_id);
  if (!profile) {
    return base::unexpected(SpacesError::kNotFound);
  }
  if (profile->is_default) {
    return base::unexpected(SpacesError::kDefaultProfile);
  }
  if (std::ranges::any_of(spaces_, [&](const SpaceEntry& space) {
        return space.profile_id == profile_id;
      })) {
    return base::unexpected(SpacesError::kInUse);
  }
  return base::ok();
}

base::expected<void, SpacesError> CluSpaceList::RemoveProfile(
    std::string_view profile_id) {
  base::expected<void, SpacesError> allowed = CanRemoveProfile(profile_id);
  if (!allowed.has_value()) {
    return allowed;
  }
  const std::string id(profile_id);
  std::erase_if(profiles_,
                [&](const ProfileEntry& profile) { return profile.id == id; });
  return base::ok();
}

bool CluSpaceList::ForgetProfile(std::string_view profile_id) {
  const ProfileEntry* profile = FindProfile(profile_id);
  const ProfileEntry* fallback = DefaultProfile();
  if (!profile || profile->is_default || !fallback) {
    return false;
  }
  const std::string fallback_id = fallback->id;
  const std::string id(profile_id);
  for (SpaceEntry& space : spaces_) {
    if (space.profile_id == id) {
      space.profile_id = fallback_id;
    }
  }
  std::erase_if(profiles_,
                [&](const ProfileEntry& entry) { return entry.id == id; });
  return true;
}

std::vector<std::string> CluSpaceList::RemovableAtLaunch(
    const std::set<std::string>& candidates,
    const std::set<std::string>& kept) const {
  std::vector<std::string> ids;
  std::set<std::string> going;
  for (const std::string& id : candidates) {
    const ProfileEntry* profile = FindProfile(id);
    if (!profile || profile->is_default || kept.contains(id)) {
      continue;
    }
    going.insert(id);
    if (std::ranges::none_of(spaces_, [&](const SpaceEntry& space) {
          return !going.contains(space.profile_id);
        })) {
      going.erase(id);
      continue;
    }
    ids.push_back(id);
  }
  return ids;
}

}  // namespace clu
