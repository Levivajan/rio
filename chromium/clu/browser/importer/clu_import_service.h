// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_IMPORT_SERVICE_H_
#define CLU_BROWSER_IMPORTER_CLU_IMPORT_SERVICE_H_

#include <memory>
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
#include "base/types/expected.h"
#include "clu/browser/importer/clu_arc_secrets.h"
#include "clu/browser/importer/clu_import_detect.h"
#include "clu/browser/importer/clu_sidebar_file.h"

namespace base {
class Clock;
}  // namespace base

namespace network::mojom {
class CookieManager;
}  // namespace network::mojom

// The Chrome and sidebar-file imports (docs/plan/07-data.md, docs/PLAN.md 12.13 fifth box), on Chromium.
// Files are read on a blocking pool and only read; rows are written on the UI thread through each profile's
// CluModelService, history through the profile's HistoryService, spaces and profiles through CluSpacesService and
// themes through CluAppSettings.
//
// - Chrome: bookmarks land as a pinned folder "Imported from Chrome" in the target space (its profile's model), with
//   "Bookmarks bar", "Other bookmarks" and "Mobile bookmarks" and the original tree inside; history becomes visits of
//   the target space's profile. The profile's clu.db import map makes a re-import add nothing.
// - Site data (clu_site_data.h): after the passwords and cookies, the site storage (staged, merged
//   at the next launch) and site permissions with the sign-ins, the saved addresses always, and Chrome's default
//   search engine.
// - Passwords and cookies: after each source profile's history, copies of its Login Data and Cookies are
//   decrypted with the key from the keychain item "Arc Safe Storage" (Google Chrome's: "Chrome Safe Storage") and
//   written to the Rio profile's password store and cookie jar. A denied keychain prompt skips both and says so.
// - Sidebar file: one Clu profile (a Chromium profile) per source profile, each source space a space on it with its
//   name, emoji and theme, its pinned tree, and the profile's favorites. The import map in Local State finds the
//   profiles and spaces a previous run made; each profile's clu.db the rows. The space and profile caps are checked
//   before anything is written.
//
// Every path resolves under the import root: the home directory, the root a test sets, or the folder
// --rio-import-root names (hidden checks with the fixture; the keychain is then never asked). Under --test-type (every
// Chromium test binary) there is no root unless a test sets one, so tests never read real browser data. One import
// runs at a time. One instance for the browser process, on the UI thread.
namespace clu {

// The folder in the user data directory that holds the copy of Chrome's History while an import reads it.
inline constexpr base::FilePath::CharType kImportTempDirName[] =
    FILE_PATH_LITERAL("CluImportTemp");

enum class ImportStartError {
  // Another import is running.
  kBusy,
  // No such space, Chrome profile or sidebar file.
  kNotFound,
};

struct ImportProgress {
  // "chrome" or "arc" (the contract's name for the sidebar file).
  std::string source;
  std::string phase;
  int done = 0;
  int total = 0;
  int skipped = 0;
};

struct ChromeImportRequest {
  std::string profile_dir;
  bool bookmarks = false;
  bool history = false;
  // Saved passwords and cookies, with the key from the keychain item "Chrome Safe Storage".
  bool passwords = false;
  bool cookies = false;
  // Saved addresses into Autofill, and Chrome's default search engine as Rio's.
  bool autofill = false;
  bool search_engine = false;
  std::string target_space_id;
};

struct ChromeImportResult {
  int bookmarks = 0;
  int history = 0;
  // Into the target space's Rio profile: its password store and its cookie jar.
  int passwords = 0;
  int cookies = 0;
  // The user denied the keychain prompt (or there was no key): no passwords or cookies came over.
  bool secrets_denied = false;
  // Site data: sites whose Local Storage or IndexedDB was staged (it goes in at the next launch), site
  // permissions and saved addresses written, and the default search engine when it became Rio's.
  int site_storage = 0;
  int permissions = 0;
  int addresses = 0;
  std::optional<std::string> search_engine;
  // Includes passwords and cookies that didn't decrypt, junk rows and rows past the caps.
  int skipped = 0;
  // Set when the import stopped early; what was imported before it stays.
  std::optional<std::string> error;
};

// A Web Store extension a source profile had: its store id and its name.
struct SidebarExtension {
  std::string id;
  std::string name;
  // The Rio profile its source profile became: Reinstall installs it there.
  std::string profile_id;
};

struct SidebarImportResult {
  int profiles = 0;
  int spaces = 0;
  int pinned = 0;
  int today = 0;
  int favorites = 0;
  // Visits from each source profile's History file, into the Rio profile it became.
  int history = 0;
  // Bookmarks from each source profile's Bookmarks file, as a pinned folder in its first space.
  int bookmarks = 0;
  // Saved passwords and cookies, each into the Rio profile its source profile became.
  int passwords = 0;
  int cookies = 0;
  // The source's default search engine when it set one and it became the Rio profile's default.
  std::optional<std::string> search_engine;
  // Web Store extensions the source profiles had, for the summary's one-click reinstall.
  std::vector<SidebarExtension> extensions;
  // The user denied the keychain prompt (or there was no key): no passwords or cookies came over.
  bool secrets_denied = false;
  // Site data: sites whose Local Storage or IndexedDB was staged (it goes in at the next launch), site
  // permissions and saved addresses, each into the Rio profile its source profile became.
  int site_storage = 0;
  int permissions = 0;
  int addresses = 0;
  SidebarSkips skipped;
  // Passwords and cookies that didn't decrypt, junk rows and rows past the caps.
  SidebarSecretsSkips secrets_skipped;
  std::optional<std::string> error;
  // The Rio space the source's first space became: the window shows it once the import ends.
  std::string main_space_id;
};

// What the sidebar import brings; spaces and profiles always come over, the rest is one checkbox each.
struct SidebarImportOptions {
  bool passwords = false;
  bool cookies = false;
  bool favorites = true;
  bool pinned = true;
  bool today = true;
  bool history = true;
  bool bookmarks = true;
  bool search_engine = true;
  bool extensions = true;
  // Saved addresses into Autofill.
  bool autofill = true;
  // Re-import: the profiles a previous import made, and the "Imported N" ones a broken earlier run left,
  // go first, with their spaces; the user said yes to replacing them.
  bool replace = false;
};

struct ImportDetection {
  std::vector<ChromeProfileInfo> chrome;
  bool sidebar = false;
  // Profiles a previous sidebar import made (PreviousSidebarProfiles()); the UI asks before replacing them.
  int previous = 0;
};

class CluImportService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // app.import changed.
    virtual void OnImportProgress() = 0;
  };

  using ChromeCallback = base::OnceCallback<void(
      base::expected<ChromeImportResult, ImportStartError>)>;
  using SidebarCallback = base::OnceCallback<void(
      base::expected<SidebarImportResult, ImportStartError>)>;

  static CluImportService* Get();

  CluImportService(const CluImportService&) = delete;
  CluImportService& operator=(const CluImportService&) = delete;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // import.detect: Chrome profiles with their names, and whether a sidebar file exists. Nothing else is read.
  void Detect(base::OnceCallback<void(ImportDetection)> done);
  void RunChrome(ChromeImportRequest request, ChromeCallback done);
  // Passwords and cookies need the other browser's keychain secret: macOS asks the user once per import.
  void RunSidebar(SidebarImportOptions options, SidebarCallback done);

  bool running() const { return running_; }
  // The Rio profiles a previous sidebar import made (the import map's profile entries) and the ones a broken earlier
  // run named "Imported" or "Imported N"; never the default profile.
  std::vector<std::string> PreviousSidebarProfiles() const;
  // Drops the sidebar entries of the import map, so the next import makes its profiles and spaces again.
  void ForgetSidebarImport();
  const std::optional<ImportProgress>& progress() const { return progress_; }
  // An extension the last sidebar import listed, or null: Reinstall installs only those.
  const SidebarExtension* FindImportedExtension(std::string_view id) const;

  // Tests: the import root, the clock, and the directory History copies go to.
  void SetRootForTesting(std::optional<base::FilePath> root);
  void SetClockForTesting(const base::Clock* clock);
  // Tests: the keychain secret the import uses instead of asking the keychain; nullopt acts as a denied prompt. Under
  // --test-type without this the import never touches the keychain and acts as denied.
  void SetSafeStorageSecretForTesting(std::optional<std::string> secret);
  void SetImportedExtensionsForTesting(std::vector<SidebarExtension> extensions);
  // Tests: the cookie manager imported cookies go to instead of the profile's, or null for the profile's.
  static void SetCookieManagerForTesting(network::mojom::CookieManager* manager);

 private:
  friend base::NoDestructor<CluImportService>;
  class ChromeJob;
  class SidebarJob;

  CluImportService();
  ~CluImportService();

  // The root imports read under, or empty for none.
  base::FilePath Root() const;
  int64_t NowMs() const;
  // Reads the keychain item's secret on a blocking thread: the real keychain, the test secret, or none under
  // --test-type.
  base::OnceCallback<std::optional<std::string>()> SecretSource(const char* service, const char* account) const;
  void SetProgress(std::optional<ImportProgress> progress);
  void Finish();

  bool running_ = false;
  std::optional<ImportProgress> progress_;
  std::vector<SidebarExtension> imported_extensions_;
  std::optional<base::FilePath> root_for_testing_;
  std::optional<std::optional<std::string>> secret_for_testing_;
  raw_ptr<const base::Clock> clock_;
  std::unique_ptr<ChromeJob> chrome_job_;
  std::unique_ptr<SidebarJob> sidebar_job_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<CluImportService> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_IMPORT_SERVICE_H_
