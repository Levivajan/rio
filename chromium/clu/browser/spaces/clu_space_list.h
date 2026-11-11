// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SPACES_CLU_SPACE_LIST_H_
#define CLU_BROWSER_SPACES_CLU_SPACE_LIST_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "base/values.h"

// Clu's profiles and the space order across them (docs/plan/03-spaces.md "Shared types", docs/chromium/m4.md). Each
// Clu profile is a Chromium profile directory; each space belongs to one profile and its tab rows live in that
// profile's clu.db. This list is what spans profiles: which profiles Clu knows, the spaces in switcher order with
// their names, icons and colors, and the space the app showed last. CluSpacesService keeps it in Local State.
//
// Pure: no profile or pref code, so clu_unittests covers every rule and every junk row. Ids come from `new_id` so
// tests can pin them.
namespace clu {

struct ProfileEntry {
  ProfileEntry();
  ProfileEntry(const ProfileEntry&);
  ProfileEntry& operator=(const ProfileEntry&);
  ~ProfileEntry();

  // UUID v4 (ProfileState.id), never shown.
  std::string id;
  // The profile directory's base name under the user data directory ("Default", "Profile 1").
  std::string dir;
  std::string name;
  bool is_default = false;
};

struct SpaceEntry {
  SpaceEntry();
  SpaceEntry(const SpaceEntry&);
  SpaceEntry& operator=(const SpaceEntry&);
  ~SpaceEntry();

  std::string id;
  std::string name;
  // "letter", "emoji" or "symbol".
  std::string icon_kind = "letter";
  // One emoji for icon_kind "emoji", one of SPACE_SYMBOLS for "symbol", else empty.
  std::string icon_value;
  std::string color = "blue";
  std::string profile_id;
  // The profile Local State named for the space when the load had no such profile (a junk or duplicate profile row):
  // the space waits on the default profile, and goes back when a profile with this id is adopted, since its rows are
  // still in that profile's clu.db. Empty otherwise.
  std::string lost_profile_id;
};

// Why the list refused a change. The IpcError the handlers answer comes from SpacesErrorCode() and
// SpacesErrorReason().
enum class SpacesError {
  kNotFound,
  kBadPayload,
  kMaxSpaces,
  kMaxProfiles,
  kLastSpace,
  kDefaultProfile,
  kInUse,
};
// "not-found", "bad-payload", "max-spaces", "max-profiles", "last-space", "default-profile", "in-use".
std::string_view SpacesErrorReason(SpacesError error);

// What load-time repair fixed (RepairFix in src/main/spaces/repair.ts), for one log line and the tests.
enum class SpacesFix {
  kProfileJunk,
  kProfileName,
  kDefaultPromoted,
  kDefaultDuplicate,
  kSpaceJunk,
  kSpaceProfile,
  kSpaceColor,
  kSpaceIcon,
  kSpaceName,
  kLastSpace,
};

// A space icon from a caller: a letter, one emoji (refused unless ParseEmoji() accepts it), or a symbol (refused
// unless IsSpaceSymbol() accepts it).
struct SpaceIconInput {
  bool emoji = false;
  std::string value;
  bool symbol = false;
};

class CluSpaceList {
 public:
  using IdSource = base::RepeatingCallback<std::string()>;

  // `new_id` makes UUID v4 ids (tests pass a fixed sequence).
  explicit CluSpaceList(IdSource new_id);
  CluSpaceList(const CluSpaceList&) = delete;
  CluSpaceList& operator=(const CluSpaceList&) = delete;
  ~CluSpaceList();

  // Reads the stored lists, repairing every junk row: entries that are not dictionaries or lack a UUID v4 id (and,
  // for profiles, a safe directory name) are dropped, duplicates too; names are renormalized or become "Profile N" /
  // "Space N"; exactly one profile is the default; a space whose profile is unknown moves to the default profile; an
  // unknown color becomes slate; an icon that is not one emoji or a known symbol becomes the letter icon; a last space that does not
  // exist is cleared. More than 50 spaces or 20 profiles are kept, but creating more is refused. Null lists read as
  // empty. Returns the fixes made.
  std::set<SpacesFix> Load(const base::ListValue* profiles,
                           const base::ListValue* spaces,
                           std::string_view last_space);
  base::ListValue ProfilesValue() const;
  base::ListValue SpacesValue() const;

  const std::vector<ProfileEntry>& profiles() const { return profiles_; }
  // In switcher order.
  const std::vector<SpaceEntry>& spaces() const { return spaces_; }
  std::vector<std::string> Order() const;
  const std::string& last_space() const { return last_space_; }
  // Remembers the space a window showed last (an unknown id is ignored).
  void SetLastSpace(std::string_view space_id);

  const ProfileEntry* FindProfile(std::string_view id) const;
  const ProfileEntry* FindProfileByDir(std::string_view dir) const;
  const ProfileEntry* DefaultProfile() const;
  const SpaceEntry* FindSpace(std::string_view id) const;
  // The spaces of one profile, in switcher order.
  std::vector<SpaceEntry> SpacesOf(std::string_view profile_id) const;
  int IndexOf(std::string_view space_id) const;

  // A profile directory Clu has not seen (first run, or Local State lost): it joins with the id and spaces its own
  // clu.db holds, so its rows keep their spaces. The first profile adopted is the default. Spaces whose id the list
  // already has are skipped; the 50-space cap does not apply (nothing may be lost). Returns the profile id used: its
  // own `id` when that is a UUID v4 no other profile uses, else a fresh one.
  std::string AdoptProfile(std::string id,
                           std::string dir,
                           std::vector<SpaceEntry> spaces);

  // spaces.create: `name` normalized or the next "Space N", the letter icon and indigo by default, the default profile
  // or `profile_id` (an unknown one is refused). Refuses past 50 spaces and an icon that is not one emoji or a known symbol.
  base::expected<std::string, SpacesError> CreateSpace(
      const std::optional<std::string>& name,
      const std::optional<SpaceIconInput>& icon,
      const std::optional<std::string>& color,
      const std::optional<std::string>& profile_id);
  // spaces.update: true when something changed. An empty name or a bad icon or color is refused.
  base::expected<bool, SpacesError> UpdateSpace(
      std::string_view space_id,
      const std::optional<std::string>& name,
      const std::optional<SpaceIconInput>& icon,
      const std::optional<std::string>& color);
  // spaces.reorder, clamped to 0..n-1: true when the order changed.
  base::expected<bool, SpacesError> ReorderSpace(std::string_view space_id,
                                                 int to_index);
  // spaces.delete: the last space is refused. Returns the space a window showing it switches to.
  base::expected<std::string, SpacesError> DeleteSpace(
      std::string_view space_id);
  // spaces.setProfile: true when the profile changed.
  base::expected<bool, SpacesError> SetSpaceProfile(std::string_view space_id,
                                                    std::string_view profile_id);

  // profiles.create: `name` normalized or the next "Profile N", in `dir` (a new Chromium profile directory). Refuses
  // past 20 profiles.
  base::expected<std::string, SpacesError> AddProfile(
      const std::optional<std::string>& name,
      std::string dir);
  // profiles.rename: true when the name changed; an empty name is refused.
  base::expected<bool, SpacesError> RenameProfile(std::string_view profile_id,
                                                  std::string_view name);
  // profiles.delete: the default profile and a profile a space uses are refused.
  base::expected<void, SpacesError> CanRemoveProfile(
      std::string_view profile_id) const;
  base::expected<void, SpacesError> RemoveProfile(std::string_view profile_id);
  // A profile directory Chromium no longer has (deleted outside Clu): its spaces move to the default profile and it
  // goes. False for an unknown id or the default profile, which stays.
  bool ForgetProfile(std::string_view profile_id);
  // At launch: the profiles among `candidates` that go with their spaces, in id order. Never an unknown id,
  // the default profile, one in `kept` (an import still owns it), or one whose going would leave no space.
  std::vector<std::string> RemovableAtLaunch(
      const std::set<std::string>& candidates,
      const std::set<std::string>& kept) const;

  // True when `dir` is a profile directory name Clu may store and turn into a path: one plain path component.
  static bool IsSafeDir(std::string_view dir);

 private:
  std::vector<std::string> SpaceNames() const;
  std::vector<std::string> ProfileNames() const;
  SpaceEntry* MutableSpace(std::string_view id);

  IdSource new_id_;
  std::vector<ProfileEntry> profiles_;
  std::vector<SpaceEntry> spaces_;
  std::string last_space_;
};

}  // namespace clu

#endif  // CLU_BROWSER_SPACES_CLU_SPACE_LIST_H_
