// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/spaces/clu_spaces_service.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/common/pref_names.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "clu/browser/model/clu_store.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace clu {

namespace {

std::string NewId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

std::vector<SpaceRow> RowsOf(const std::vector<SpaceEntry>& spaces) {
  std::vector<SpaceRow> rows;
  for (const SpaceEntry& space : spaces) {
    SpaceRow row;
    row.id = space.id;
    row.name = space.name;
    row.icon_kind = space.icon_kind;
    row.icon_value = space.icon_value;
    row.color = space.color;
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<SpaceEntry> EntriesOf(const std::vector<SpaceRow>& rows) {
  std::vector<SpaceEntry> spaces;
  for (const SpaceRow& row : rows) {
    SpaceEntry space;
    space.id = row.id;
    space.name = row.name;
    space.icon_kind = row.icon_kind;
    space.icon_value = row.icon_value;
    space.color = row.color;
    spaces.push_back(std::move(space));
  }
  return spaces;
}

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

ProfileManager* Profiles() {
  return g_browser_process ? g_browser_process->profile_manager() : nullptr;
}

}  // namespace

// static
CluSpacesService* CluSpacesService::Get() {
  if (!LocalState()) {
    return nullptr;
  }
  static base::NoDestructor<CluSpacesService> instance;
  return instance.get();
}

CluSpacesService::CluSpacesService()
    : list_(base::BindRepeating(&NewId)) {}

CluSpacesService::~CluSpacesService() = default;

void CluSpacesService::EnsureLoaded() {
  if (loaded_) {
    return;
  }
  PrefService* local_state = LocalState();
  if (!local_state) {
    return;
  }
  loaded_ = true;
  const std::set<SpacesFix> fixes =
      list_.Load(&local_state->GetList(kCluProfilesPref),
                 &local_state->GetList(kCluSpacesPref),
                 local_state->GetString(kCluLastSpacePref));
  bool changed = !fixes.empty();
  if (changed) {
    LOG(WARNING) << "clu: spaces repaired (" << fixes.size() << " kinds)";
  }
  // A profile directory Chromium no longer knows (deleted from its own settings) goes; its spaces move to the default
  // profile. The rows they had went with the directory.
  if (ProfileManager* profiles = Profiles()) {
    ProfileAttributesStorage& storage = profiles->GetProfileAttributesStorage();
    std::vector<std::string> gone;
    for (const ProfileEntry& profile : list_.profiles()) {
      if (!profile.is_default &&
          !storage.GetProfileAttributesWithPath(
              profiles->user_data_dir().AppendASCII(profile.dir))) {
        gone.push_back(profile.id);
      }
    }
    for (const std::string& id : gone) {
      changed = list_.ForgetProfile(id) || changed;
    }
  }
  // Clu switches profiles through spaces: Chromium's profile picker never shows at launch.
  local_state->SetBoolean(prefs::kBrowserShowProfilePickerOnStartup, false);
  if (changed) {
    Save();
  }
  RemoveLeftoverProfiles();
}

void CluSpacesService::RemoveLeftoverProfiles() {
  PrefService* local_state = LocalState();
  std::set<std::string> candidates;
  for (const base::Value& id : local_state->GetList(kCluProfilesToRemovePref)) {
    if (id.is_string()) {
      candidates.insert(id.GetString());
    }
  }
  // What an import still owns (the import map's values), and whether a sidebar import finished after a broken run
  // named its profiles "Imported N" (its profile entries, MapKey("profile:") in clu_import_service.cc). A Replace
  // before marks existed left an "Imported 4" that way: its spaces gone, its window kept it. Only such a
  // profile with no space left goes unmarked, so one the user made and uses stays.
  std::set<std::string> owned;
  bool import_done = false;
  for (const auto [key, value] : local_state->GetDict(kCluImportMapPref)) {
    if (value.is_string()) {
      owned.insert(value.GetString());
    }
    import_done = import_done || key.starts_with("sidebar|profile:");
  }
  if (import_done) {
    for (const ProfileEntry& profile : list_.profiles()) {
      if (IsBrokenImportName(profile.name) &&
          std::ranges::none_of(list_.spaces(), [&](const SpaceEntry& space) {
            return space.profile_id == profile.id;
          })) {
        candidates.insert(profile.id);
      }
    }
  }
  const std::vector<std::string> going =
      list_.RemovableAtLaunch(candidates, owned);
  {
    // A mark stays until its profile is gone (FinishDeleteProfile()); one that can't go (unknown, junk, owned) drops.
    ScopedListPrefUpdate update(local_state, kCluProfilesToRemovePref);
    update->EraseIf([&](const base::Value& id) {
      return !id.is_string() || !std::ranges::contains(going, id.GetString());
    });
  }
  if (going.empty()) {
    return;
  }
  for (const std::string& id : going) {
    std::vector<std::string> space_ids;
    for (const SpaceEntry& space : list_.spaces()) {
      if (space.profile_id == id) {
        space_ids.push_back(space.id);
      }
    }
    // RemovableAtLaunch() left another profile a space, so none of these is the last.
    for (const std::string& space_id : space_ids) {
      std::ignore = list_.DeleteSpace(space_id);
    }
  }
  LOG(WARNING) << "clu: removing " << going.size()
               << " profile(s) an import replaced";
  Save();
  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    return;
  }
  // Startup may be restoring the profile's windows (Chromium reopens its last active profiles): they close once it
  // settles, with no prompt, as the user said yes to the Replace.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](std::vector<std::string> ids) {
                       CluSpacesService* spaces = CluSpacesService::Get();
                       for (const std::string& id : ids) {
                         if (spaces) {
                           std::ignore = spaces->DeleteProfile(
                               id, /*skip_beforeunload=*/true);
                         }
                       }
                     },
                     going));
}

const CluSpaceList& CluSpacesService::list() {
  EnsureLoaded();
  return list_;
}

void CluSpacesService::Save() {
  PrefService* local_state = LocalState();
  if (!local_state) {
    return;
  }
  local_state->SetList(kCluProfilesPref, list_.ProfilesValue());
  local_state->SetList(kCluSpacesPref, list_.SpacesValue());
  local_state->SetString(kCluLastSpacePref, list_.last_space());
}

void CluSpacesService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CluSpacesService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void CluSpacesService::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnSpacesChanged();
  }
}

void CluSpacesService::PushSpaces() {
  // A copy: SetSpaces() commits, and a window may react by closing.
  const auto models = models_;
  for (const auto& [profile_id, model] : models) {
    if (models_.contains(profile_id)) {
      model->SetSpaces(RowsOf(list_.SpacesOf(profile_id)));
    }
  }
  NotifyChanged();
}

// Profiles -------------------------------------------------------------------------------------------------------------

std::string CluSpacesService::ProfileIdOf(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return std::string();
  }
  EnsureLoaded();
  const ProfileEntry* entry =
      list_.FindProfileByDir(profile->GetPath().BaseName().AsUTF8Unsafe());
  return entry ? entry->id : std::string();
}

base::FilePath CluSpacesService::ProfilePath(std::string_view profile_id) {
  EnsureLoaded();
  ProfileManager* profiles = Profiles();
  const ProfileEntry* entry = list_.FindProfile(profile_id);
  if (!profiles || !entry || !CluSpaceList::IsSafeDir(entry->dir)) {
    return base::FilePath();
  }
  return profiles->user_data_dir().AppendASCII(entry->dir);
}

Profile* CluSpacesService::LoadedProfile(std::string_view profile_id) {
  ProfileManager* profiles = Profiles();
  const base::FilePath path = ProfilePath(profile_id);
  return profiles && !path.empty() ? profiles->GetProfileByPath(path)
                                   : nullptr;
}

void CluSpacesService::LoadProfile(std::string_view profile_id,
                                   ProfileCallback callback) {
  ProfileManager* profiles = Profiles();
  const base::FilePath path = ProfilePath(profile_id);
  if (!profiles || path.empty()) {
    std::move(callback).Run(nullptr);
    return;
  }
  if (Profile* loaded = profiles->GetProfileByPath(path)) {
    std::move(callback).Run(loaded);
    return;
  }
  // Runs the callback in every case, with null when the directory is gone.
  profiles->LoadProfileByPath(path, /*incognito=*/false, std::move(callback));
}

CluModelService* CluSpacesService::LoadedModel(std::string_view profile_id) {
  const auto it = models_.find(profile_id);
  return it == models_.end() ? nullptr : it->second.get();
}

void CluSpacesService::CreateProfile(const std::optional<std::string>& name,
                                     CreateProfileCallback callback) {
  EnsureLoaded();
  ProfileManager* profiles = Profiles();
  if (list_.profiles().size() >= kMaxProfiles) {
    std::move(callback).Run(base::unexpected(SpacesError::kMaxProfiles));
    return;
  }
  if (!profiles) {
    std::move(callback).Run(base::unexpected(SpacesError::kNotFound));
    return;
  }
  const base::FilePath path = profiles->GenerateNextProfileDirectoryPath();
  base::expected<std::string, SpacesError> added =
      list_.AddProfile(name, path.BaseName().AsUTF8Unsafe());
  if (!added.has_value()) {
    std::move(callback).Run(std::move(added));
    return;
  }
  Save();
  NotifyChanged();
  profiles->CreateProfileAsync(
      path, base::BindOnce(&CluSpacesService::OnProfileCreated,
                           weak_ptr_factory_.GetWeakPtr(), added.value(),
                           std::move(callback)));
}

void CluSpacesService::OnProfileCreated(std::string profile_id,
                                        CreateProfileCallback callback,
                                        Profile* profile) {
  const ProfileEntry* entry = list_.FindProfile(profile_id);
  if (!profile || !entry) {
    // The directory could not be made: the entry goes again, so no "Profile N" stays behind.
    if (entry) {
      std::ignore = list_.RemoveProfile(profile_id);
      Save();
      NotifyChanged();
    }
    std::move(callback).Run(base::unexpected(SpacesError::kNotFound));
    return;
  }
  if (ProfileManager* profiles = Profiles()) {
    if (ProfileAttributesEntry* attributes =
            profiles->GetProfileAttributesStorage()
                .GetProfileAttributesWithPath(profile->GetPath())) {
      attributes->SetLocalProfileName(base::UTF8ToUTF16(entry->name),
                                      /*is_default_name=*/false);
    }
  }
  std::move(callback).Run(profile_id);
}

base::expected<void, SpacesError> CluSpacesService::RenameProfile(
    std::string_view profile_id,
    std::string_view name) {
  EnsureLoaded();
  base::expected<bool, SpacesError> renamed =
      list_.RenameProfile(profile_id, name);
  if (!renamed.has_value()) {
    return base::unexpected(renamed.error());
  }
  if (!renamed.value()) {
    return base::ok();
  }
  Save();
  ProfileManager* profiles = Profiles();
  const base::FilePath path = ProfilePath(profile_id);
  if (profiles && !path.empty()) {
    if (ProfileAttributesEntry* attributes =
            profiles->GetProfileAttributesStorage()
                .GetProfileAttributesWithPath(path)) {
      attributes->SetLocalProfileName(
          base::UTF8ToUTF16(list_.FindProfile(profile_id)->name),
          /*is_default_name=*/false);
    }
  }
  NotifyChanged();
  return base::ok();
}

base::expected<void, SpacesError> CluSpacesService::DeleteProfile(
    std::string_view profile_id,
    bool skip_beforeunload) {
  EnsureLoaded();
  base::expected<void, SpacesError> allowed =
      list_.CanRemoveProfile(profile_id);
  if (!allowed.has_value()) {
    return allowed;
  }
  ProfileManager* profiles = Profiles();
  const base::FilePath path = ProfilePath(profile_id);
  Profile* profile =
      profiles && !path.empty() ? profiles->GetProfileByPath(path) : nullptr;
  if (!profile) {
    FinishDeleteProfile(std::string(profile_id));
    return base::ok();
  }
  // DeleteProfileHelper closes the profile's windows (hidden ones a space switch left, its private windows) and gives
  // up when a page answers beforeunload with Stay. Clu closes them first, so it forgets the profile only when
  // Chromium will delete it.
  chrome::CloseAllBrowsersWithProfile(
      profile, skip_beforeunload,
      base::BindRepeating(
          [](const std::string& id, const base::FilePath&) {
            if (CluSpacesService* spaces = CluSpacesService::Get()) {
              spaces->FinishDeleteProfile(id);
            }
          },
          std::string(profile_id)));
  return base::ok();
}

void CluSpacesService::FinishDeleteProfile(const std::string& profile_id) {
  // A space may have joined the profile while its windows closed.
  if (!list_.CanRemoveProfile(profile_id).has_value()) {
    return;
  }
  const base::FilePath path = ProfilePath(profile_id);
  std::ignore = list_.RemoveProfile(profile_id);
  if (PrefService* local_state = LocalState()) {
    ScopedListPrefUpdate(local_state, kCluProfilesToRemovePref)
        ->EraseValue(base::Value(profile_id));
  }
  if (const auto it = models_.find(profile_id); it != models_.end()) {
    model_observations_.RemoveObservation(it->second.get());
    models_.erase(it);
  }
  Save();
  NotifyChanged();
  ProfileManager* profiles = Profiles();
  if (profiles && !path.empty()) {
    // Unloads the profile and removes its directory: at once when it was never loaded, else on the next start
    // (DeleteProfileHelper's CleanUpDeletedProfiles()).
    profiles->GetDeleteProfileHelper().MaybeScheduleProfileForDeletion(
        path, base::DoNothing(), ProfileMetrics::DELETE_PROFILE_SETTINGS);
  }
}

void CluSpacesService::MarkProfilesForRemoval(
    const std::vector<std::string>& profile_ids) {
  EnsureLoaded();
  PrefService* local_state = LocalState();
  if (!local_state) {
    return;
  }
  {
    ScopedListPrefUpdate update(local_state, kCluProfilesToRemovePref);
    for (const std::string& id : profile_ids) {
      const ProfileEntry* profile = list_.FindProfile(id);
      if (profile && !profile->is_default && !update->contains(id)) {
        update->Append(id);
      }
    }
  }
  // On disk at once: a quit or crash inside Local State's commit interval must not lose the marks.
  local_state->CommitPendingWrite();
}

// Spaces ---------------------------------------------------------------------------------------------------------------

base::expected<std::string, SpacesError> CluSpacesService::CreateSpace(
    const std::optional<std::string>& name,
    const std::optional<SpaceIconInput>& icon,
    const std::optional<std::string>& color,
    const std::optional<std::string>& profile_id) {
  EnsureLoaded();
  base::expected<std::string, SpacesError> id =
      list_.CreateSpace(name, icon, color, profile_id);
  if (id.has_value()) {
    Save();
    PushSpaces();
  }
  return id;
}

base::expected<void, SpacesError> CluSpacesService::UpdateSpace(
    std::string_view space_id,
    const std::optional<std::string>& name,
    const std::optional<SpaceIconInput>& icon,
    const std::optional<std::string>& color) {
  EnsureLoaded();
  base::expected<bool, SpacesError> changed =
      list_.UpdateSpace(space_id, name, icon, color);
  if (!changed.has_value()) {
    return base::unexpected(changed.error());
  }
  if (changed.value()) {
    Save();
    // The rows move between the two clu.db files now; Local State must not wait out its 10 s commit interval, or a
    // crash inside it leaves the registry naming the old profile for rows that have left it.
    if (PrefService* local_state = LocalState()) {
      local_state->CommitPendingWrite();
    }
    PushSpaces();
  }
  return base::ok();
}

base::expected<void, SpacesError> CluSpacesService::ReorderSpace(
    std::string_view space_id,
    int to_index) {
  EnsureLoaded();
  base::expected<bool, SpacesError> changed =
      list_.ReorderSpace(space_id, to_index);
  if (!changed.has_value()) {
    return base::unexpected(changed.error());
  }
  if (changed.value()) {
    Save();
    // The rows move between the two clu.db files now; Local State must not wait out its 10 s commit interval, or a
    // crash inside it leaves the registry naming the old profile for rows that have left it.
    if (PrefService* local_state = LocalState()) {
      local_state->CommitPendingWrite();
    }
    PushSpaces();
  }
  return base::ok();
}

base::expected<std::string, SpacesError> CluSpacesService::DeleteSpace(
    std::string_view space_id) {
  EnsureLoaded();
  base::expected<std::string, SpacesError> fallback =
      list_.DeleteSpace(space_id);
  if (fallback.has_value()) {
    Save();
    PushSpaces();
  }
  return fallback;
}

base::expected<void, SpacesError> CluSpacesService::SetSpaceProfile(
    std::string_view space_id,
    std::string_view profile_id) {
  EnsureLoaded();
  base::expected<bool, SpacesError> changed =
      list_.SetSpaceProfile(space_id, profile_id);
  if (!changed.has_value()) {
    return base::unexpected(changed.error());
  }
  if (changed.value()) {
    Save();
    // The rows move between the two clu.db files now; Local State must not wait out its 10 s commit interval, or a
    // crash inside it leaves the registry naming the old profile for rows that have left it.
    if (PrefService* local_state = LocalState()) {
      local_state->CommitPendingWrite();
    }
    PushSpaces();
  }
  return base::ok();
}

void CluSpacesService::SetLastSpace(std::string_view space_id) {
  EnsureLoaded();
  if (list_.last_space() == space_id) {
    return;
  }
  list_.SetLastSpace(space_id);
  if (PrefService* local_state = LocalState()) {
    local_state->SetString(kCluLastSpacePref, list_.last_space());
  }
}

// Models ---------------------------------------------------------------------------------------------------------------

void CluSpacesService::AttachModel(const base::FilePath& profile_path,
                                   CluModelService* model) {
  EnsureLoaded();
  if (!model || !model->loaded()) {
    return;
  }
  // A profile on its way out never joins again.
  if (IsProfileDirectoryMarkedForDeletion(profile_path)) {
    return;
  }
  const std::string dir = profile_path.BaseName().AsUTF8Unsafe();
  if (!CluSpaceList::IsSafeDir(dir)) {
    LOG(ERROR) << "clu: a profile directory Clu cannot store";
    return;
  }
  std::string profile_id;
  if (const ProfileEntry* known = list_.FindProfileByDir(dir)) {
    profile_id = known->id;
  } else {
    profile_id =
        list_.AdoptProfile(model->profile_id(), dir, EntriesOf(model->spaces()));
  }
  if (list_.spaces().empty()) {
    std::ignore = list_.CreateSpace(std::string(kHomeSpaceName), std::nullopt,
                                    std::string(kHomeSpaceColor), profile_id);
  }
  model->SetProfileId(profile_id);
  if (!models_.contains(profile_id)) {
    models_[profile_id] = model;
    model_observations_.AddObservation(model);
  }
  Save();
  PushSpaces();
}

void CluSpacesService::OnModelShuttingDown(CluModelService* model) {
  for (auto it = models_.begin(); it != models_.end(); ++it) {
    if (it->second == model) {
      models_.erase(it);
      break;
    }
  }
  if (model_observations_.IsObservingSource(model)) {
    model_observations_.RemoveObservation(model);
  }
}

void CluSpacesService::AddProfileForTesting(const std::string& dir) {
  EnsureLoaded();
  if (list_.AddProfile(std::nullopt, dir).has_value()) {
    Save();
    NotifyChanged();
  }
}

void CluSpacesService::ResetForTesting() {
  model_observations_.RemoveAllObservations();
  models_.clear();
  loaded_ = false;
}

}  // namespace clu
