// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SPACES_CLU_SPACES_SERVICE_H_
#define CLU_BROWSER_SPACES_CLU_SPACES_SERVICE_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_multi_source_observation.h"
#include "base/types/expected.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/spaces/clu_space_list.h"
#include "clu/browser/spaces/clu_spaces_prefs.h"

class Profile;

// Clu's spaces and profiles across the browser (docs/chromium/m4.md). It keeps the CluSpaceList in Local State, makes
// each Clu profile a real Chromium profile (ProfileManager, ProfileAttributesStorage, DeleteProfileHelper), and tells
// each profile's CluModelService which spaces are its own. Profiles load lazily: only when a window shows one of
// their spaces, or when an action needs their rows.
//
// Only the registry lives here. What moves rows and pages between profiles and windows (switching, deleting a space,
// changing a space's profile, a cross-profile move) is in clu/browser/ui/webui/clu_space_actions.h, which calls these
// in order. One instance for the browser process, on the UI thread; it keeps no pointer to Chromium objects other than
// the models it observes, so the process can tear down in any order.
namespace clu {

class CluSpacesService : public CluModelService::Observer {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // Spaces or profiles changed: windows republish their state.
    virtual void OnSpacesChanged() = 0;
  };

  using ProfileCallback = base::OnceCallback<void(Profile*)>;
  using CreateProfileCallback =
      base::OnceCallback<void(base::expected<std::string, SpacesError>)>;

  // Null without Local State (clu_unittests, early startup, shutdown).
  static CluSpacesService* Get();

  CluSpacesService(const CluSpacesService&) = delete;
  CluSpacesService& operator=(const CluSpacesService&) = delete;

  // The list, read from Local State on first use.
  const CluSpaceList& list();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // ---- profiles ----------------------------------------------------------------------------------------------------

  // The Clu id of a regular profile, empty for an off-the-record profile or a directory Clu has not seen.
  std::string ProfileIdOf(Profile* profile);
  base::FilePath ProfilePath(std::string_view profile_id);
  // The profile when it is loaded, else null.
  Profile* LoadedProfile(std::string_view profile_id);
  // Loads the profile (at once when it is loaded); null when it cannot be (unknown id, directory gone).
  void LoadProfile(std::string_view profile_id, ProfileCallback callback);
  // The loaded model of a profile, once its rows are read; else null.
  CluModelService* LoadedModel(std::string_view profile_id);

  // profiles.create: a new Chromium profile directory with Clu's name. Answers once the profile exists.
  void CreateProfile(const std::optional<std::string>& name,
                     CreateProfileCallback callback);
  // profiles.rename: Chromium's own name for the profile follows.
  base::expected<void, SpacesError> RenameProfile(std::string_view profile_id,
                                                  std::string_view name);
  // profiles.delete: refuses the default profile and one a space uses. The profile's windows close first (their
  // pages may answer beforeunload with Stay, which keeps the profile); then Clu forgets it and Chromium's
  // DeleteProfileHelper unloads it and removes its directory, at the latest on the next start.
  // `skip_beforeunload`: an import Replace the user said yes to, whose windows were moved off and hidden, so
  // no one could answer a page's prompt there.
  base::expected<void, SpacesError> DeleteProfile(std::string_view profile_id,
                                                  bool skip_beforeunload = false);
  // An import Replace removes these profiles. They stay marked until they are gone, so one a window kept
  // (a quit or a page cut the close short) is removed at the next launch. Never the default profile.
  void MarkProfilesForRemoval(const std::vector<std::string>& profile_ids);

  // ---- spaces (the registry only) ----------------------------------------------------------------------------------

  base::expected<std::string, SpacesError> CreateSpace(
      const std::optional<std::string>& name,
      const std::optional<SpaceIconInput>& icon,
      const std::optional<std::string>& color,
      const std::optional<std::string>& profile_id);
  base::expected<void, SpacesError> UpdateSpace(
      std::string_view space_id,
      const std::optional<std::string>& name,
      const std::optional<SpaceIconInput>& icon,
      const std::optional<std::string>& color);
  base::expected<void, SpacesError> ReorderSpace(std::string_view space_id,
                                                 int to_index);
  // The space leaves the list; its rows must have left its model already (CluModelService::DeleteSpaceRows()).
  // Returns the space windows that showed it switch to.
  base::expected<std::string, SpacesError> DeleteSpace(
      std::string_view space_id);
  // The space's rows must have left the old profile's model already (CluModelService::TakeSpaceRows()); the new
  // profile must be loaded so its model can take them right after.
  base::expected<void, SpacesError> SetSpaceProfile(std::string_view space_id,
                                                    std::string_view profile_id);
  // The space a window showed last: a new window and the next launch start there.
  void SetLastSpace(std::string_view space_id);

  // ---- models ------------------------------------------------------------------------------------------------------

  // The factory's hook once a profile's rows are read: a directory Clu has not seen joins with the spaces its clu.db
  // mirrors (first run, or Local State lost), the model learns its Clu id and its spaces, and with no space at all a
  // "Home" space is made on it.
  void AttachModel(const base::FilePath& profile_path, CluModelService* model);

  // CluModelService::Observer:
  void OnModelChanged() override {}
  void OnModelShuttingDown(CluModelService* model) override;

  // Tests: forget everything read so far, so the next use reads Local State again.
  void ResetForTesting();
  // Tests: a registry profile in `dir` with no Chromium profile behind it (the 20-profile cap).
  void AddProfileForTesting(const std::string& dir);

 private:
  friend base::NoDestructor<CluSpacesService>;

  CluSpacesService();
  ~CluSpacesService() override;

  void EnsureLoaded();
  // At launch: the marked profiles, and the "Imported N" ones a broken earlier import left once a later one
  // finished, lose their spaces now and go once startup settles.
  void RemoveLeftoverProfiles();
  void Save();
  // DeleteProfile() once the profile's windows closed.
  void FinishDeleteProfile(const std::string& profile_id);
  // Spaces changed: every loaded model gets its own, windows republish.
  void PushSpaces();
  void NotifyChanged();
  void OnProfileCreated(std::string profile_id,
                        CreateProfileCallback callback,
                        Profile* profile);

  CluSpaceList list_;
  bool loaded_ = false;
  std::map<std::string, raw_ptr<CluModelService>, std::less<>> models_;
  base::ScopedMultiSourceObservation<CluModelService, CluModelService::Observer>
      model_observations_{this};
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<CluSpacesService> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_SPACES_CLU_SPACES_SERVICE_H_
