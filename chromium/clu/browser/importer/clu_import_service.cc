// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_import_service.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include "base/barrier_callback.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "base/types/expected.h"
#include "base/uuid.h"
#include "base/values.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/password_manager/factories/profile_password_store_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/common/chrome_paths.h"
#include "clu/browser/importer/clu_arc_keychain.h"
#include "clu/browser/importer/clu_chrome_bookmarks.h"
#include "clu/browser/importer/clu_chrome_history.h"
#include "clu/browser/importer/clu_import_plan.h"
#include "clu/browser/importer/clu_import_rules.h"
#include "clu/browser/importer/clu_site_data.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/multitask/clu_split_rules.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "components/autofill/core/browser/data_manager/addresses/address_data_manager.h"
#include "components/autofill/core/browser/data_manager/personal_data_manager.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_profile.h"
#include "components/autofill/core/browser/data_model/addresses/autofill_structured_address_component.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/password_manager/core/browser/password_store/password_store_interface.h"
#include "components/password_manager/core/browser/password_store/stored_credential.h"
#include "components/password_manager/core/browser/password_string.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_inclusion_status.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_partition_key.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "url/gurl.h"
#include "url/third_party/mozilla/url_parse.h"

namespace clu {

namespace {

// Import map sources in each profile's clu.db, and the key prefix of Local State's map.
constexpr char kChromeSource[] = "chrome";
constexpr char kChromeHistorySource[] = "chrome-history";
constexpr char kSidebarSource[] = "sidebar";
constexpr char kSidebarHistorySource[] = "sidebar-history";
constexpr char kNoProfile[] = "no such Chrome profile";
constexpr char kCannotFinish[] = "The import couldn’t finish.";
constexpr char kTooManySpaces[] =
    "Rio can’t hold that many spaces. Delete some spaces and try again.";
constexpr char kTooManyProfiles[] =
    "Rio can’t hold that many profiles. Delete a profile and try again.";

// Hidden checks point the import at the fixture folder with this switch; the keychain is then never asked.
constexpr char kImportRootSwitch[] = "rio-import-root";
// The pinned folder a source profile's bookmarks land in.
constexpr char kSidebarBookmarksTitle[] = "Imported bookmarks";
// Extensions past this count are not listed.
constexpr size_t kMaxExtensions = 500;

constexpr base::TaskTraits kBlocking = {
    base::MayBlock(), base::TaskPriority::USER_VISIBLE,
    base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN};

std::string NewId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

std::vector<std::string> KeysOf(const std::vector<ImportNode>& nodes) {
  std::vector<std::string> keys;
  keys.reserve(nodes.size());
  for (const ImportNode& node : nodes) {
    keys.push_back(node.key);
  }
  return keys;
}

// Local State's import map: {"sidebar|profile:<key>": profile id, "sidebar|space:<key>": space id}.
std::string MapKey(std::string_view key) {
  return base::StrCat({kSidebarSource, "|", key});
}

std::optional<std::string> MappedId(std::string_view key) {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  const std::string* id =
      local_state ? local_state->GetDict(kCluImportMapPref).FindString(MapKey(key))
                  : nullptr;
  return id ? std::optional<std::string>(*id) : std::nullopt;
}

void Remember(std::string_view key, const std::string& id) {
  if (PrefService* local_state =
          g_browser_process ? g_browser_process->local_state() : nullptr) {
    ScopedDictPrefUpdate update(local_state, kCluImportMapPref);
    update->Set(MapKey(key), id);
  }
}

struct BookmarksRead {
  bool found = false;
  base::FilePath dir;
  BookmarksResult parsed;
};

BookmarksRead ReadBookmarks(base::FilePath root,
                            std::string profile_dir,
                            bool parse) {
  BookmarksRead read;
  const std::optional<base::FilePath> dir = ChromeProfilePath(root, profile_dir);
  if (!dir) {
    return read;
  }
  read.found = true;
  read.dir = *dir;
  if (!parse) {
    return read;
  }
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(dir->Append("Bookmarks"), &text,
                                         kMaxBookmarksBytes)) {
    read.parsed.error = kBookmarksDamaged;
    return read;
  }
  read.parsed = ParseChromeBookmarks(text, profile_dir);
  return read;
}

struct SidebarRead {
  bool found = false;
  SidebarPlan plan;
};

// Local State in the other browser's User Data keeps each profile folder's name (profile.info_cache.<dir>.name):
// the names its spaces' profiles have there become the Rio profiles' names. Missing or junk names stay
// empty.
constexpr size_t kMaxSourceLocalStateBytes = 4 * 1024 * 1024;

void ReadSourceProfileNames(const base::FilePath& root, SidebarPlan& plan) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(
          SidebarUserDataDir(root).Append("Local State"), &text,
          kMaxSourceLocalStateBytes)) {
    return;
  }
  const std::optional<base::Value> state =
      base::JSONReader::Read(text, base::JSON_PARSE_RFC);
  const base::DictValue* cache =
      state && state->is_dict()
          ? state->GetDict().FindDictByDottedPath("profile.info_cache")
          : nullptr;
  if (!cache) {
    return;
  }
  for (SidebarProfilePlan& p : plan.profiles) {
    const std::optional<base::FilePath> dir = SidebarProfileDir(root, p.key);
    const base::DictValue* info =
        dir ? cache->FindDict(dir->BaseName().AsUTF8Unsafe()) : nullptr;
    if (info) {
      p.name = ImportTitle(info->Find("name"));
    }
  }
}

SidebarRead ReadSidebar(base::FilePath root) {
  SidebarRead read;
  if (!HasSidebarFile(root)) {
    return read;
  }
  read.found = true;
  const base::FilePath file = SidebarFilePath(root);
  base::File::Info info;
  if (!base::GetFileInfo(file, &info)) {
    read.plan.error = kSidebarUnreadable;
    return read;
  }
  // Refused without reading it.
  if (info.size > static_cast<int64_t>(kMaxSidebarBytes)) {
    read.plan.error = kSidebarTooLarge;
    return read;
  }
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(file, &text, kMaxSidebarBytes)) {
    read.plan.error = kSidebarUnreadable;
    return read;
  }
  read.plan = ParseSidebarFile(text);
  if (!read.plan.error) {
    ReadSourceProfileNames(root, read.plan);
  }
  return read;
}

ImportDetection DetectAll(base::FilePath root) {
  ImportDetection detection;
  detection.chrome = DetectChrome(root);
  detection.sidebar = HasSidebarFile(root);
  return detection;
}

// History copies go under Rio's own data folder, never the system temp folder, so a copy a quit left behind is
// within what Rio owns and is swept at the next start.
base::FilePath TempRoot() {
  base::FilePath dir;
  return base::PathService::Get(chrome::DIR_USER_DATA, &dir)
             ? dir.Append(kImportTempDirName)
             : base::FilePath();
}

// Reads of Chrome's History run here, in order after the sweep. Shutdown waits for a read that started, so its copy
// is always removed.
scoped_refptr<base::SequencedTaskRunner> HistoryRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));
  return *runner;
}

// A sidebar profile's History, found and read off the UI thread; nullopt when the profile has no History file.
std::optional<HistoryReadResult> ReadSidebarHistory(const base::FilePath& root,
                                                    const std::string& key,
                                                    int64_t now_ms,
                                                    const base::FilePath& temp_root) {
  const std::optional<base::FilePath> path = SidebarHistoryPath(root, key);
  if (!path) {
    return std::nullopt;
  }
  return ReadChromeHistory(*path, now_ms, temp_root);
}

// A source profile's bookmarks, default search engine and Web Store extensions, read off the UI thread.
using SidebarSearchEngine = ImportedSearchEngine;

struct SidebarExtrasRead {
  BookmarksResult bookmarks;
  std::optional<SidebarSearchEngine> search_engine;
  std::vector<SidebarExtension> extensions;
};

// A Web Store id: 32 letters a to p.
bool IsExtensionId(std::string_view id) {
  return id.size() == 32 &&
         std::all_of(id.begin(), id.end(), [](char c) { return c >= 'a' && c <= 'p'; });
}

// "__MSG_appName__" from the extension's default locale (or English); the raw name otherwise.
std::string ExtensionName(const base::FilePath& dir, const base::DictValue& manifest) {
  const std::string* name = manifest.FindString("name");
  if (!name) {
    return std::string();
  }
  if (!name->starts_with("__MSG_") || !name->ends_with("__") || name->size() <= 8) {
    return *name;
  }
  const std::string key = base::ToLowerASCII(name->substr(6, name->size() - 8));
  const std::string* locale = manifest.FindString("default_locale");
  for (const std::string& l : {locale ? *locale : std::string("en"), std::string("en")}) {
    if (l.empty() || l.find_first_of("/\\.") != std::string::npos) {
      continue;
    }
    std::optional<base::DictValue> messages =
        ReadJsonDict(dir.Append("_locales").AppendASCII(l).Append("messages.json"));
    if (!messages) {
      continue;
    }
    for (const auto [k, v] : *messages) {
      const std::string* text = v.is_dict() ? v.GetDict().FindString("message") : nullptr;
      if (text && base::ToLowerASCII(k) == key) {
        return *text;
      }
    }
  }
  return std::string();
}

// The newest installed version folder of an extension: <profile>/Extensions/<id>/<version>.
std::optional<base::FilePath> ExtensionDir(const base::FilePath& profile, const std::string& id) {
  base::FileEnumerator versions(profile.Append("Extensions").AppendASCII(id), false,
                                base::FileEnumerator::DIRECTORIES);
  std::optional<base::FilePath> last;
  for (base::FilePath p = versions.Next(); !p.empty(); p = versions.Next()) {
    if (!last || p.BaseName().value() > last->BaseName().value()) {
      last = p;
    }
  }
  return last;
}

void ReadExtensions(const base::FilePath& profile, const base::DictValue& prefs,
                    std::vector<SidebarExtension>& out) {
  const base::DictValue* settings = prefs.FindDictByDottedPath("extensions.settings");
  if (!settings) {
    return;
  }
  for (const auto [id, entry] : *settings) {
    if (out.size() >= kMaxExtensions || !IsExtensionId(id) || !entry.is_dict()) {
      continue;
    }
    const base::DictValue& e = entry.GetDict();
    // Web Store installs only: location 1 is an internal (store) install; component and policy ones stay behind.
    if (!e.FindBool("from_webstore").value_or(false) && e.FindInt("location").value_or(0) != 1) {
      continue;
    }
    if (std::any_of(out.begin(), out.end(), [&](const SidebarExtension& x) { return x.id == id; })) {
      continue;
    }
    std::string name;
    const std::optional<base::FilePath> dir = ExtensionDir(profile, id);
    if (const base::DictValue* manifest = e.FindDict("manifest")) {
      name = ExtensionName(dir.value_or(base::FilePath()), *manifest);
    }
    if (name.empty() && dir) {
      if (std::optional<base::DictValue> manifest = ReadJsonDict(dir->Append("manifest.json"))) {
        name = ExtensionName(*dir, *manifest);
      }
    }
    // Junk names are cut to a row's worth.
    out.push_back({.id = id, .name = name.empty() ? id : name.substr(0, 200)});
  }
}

SidebarExtrasRead ReadSidebarExtras(const base::FilePath& root,
                                    const std::string& key,
                                    SidebarImportOptions options) {
  SidebarExtrasRead read;
  const std::optional<base::FilePath> dir = SidebarProfileDir(root, key);
  if (!dir || !base::DirectoryExists(*dir)) {
    return read;
  }
  if (options.bookmarks) {
    std::string text;
    const base::FilePath file = dir->Append("Bookmarks");
    if (base::PathExists(file) && !base::IsLink(file) &&
        base::ReadFileToStringWithMaxSize(file, &text, kMaxBookmarksBytes)) {
      read.bookmarks = ParseChromeBookmarks(text, "sidebar-bookmarks:" + key);
      if (!read.bookmarks.nodes.empty()) {
        read.bookmarks.nodes.front().title = kSidebarBookmarksTitle;
      }
    }
  }
  if (!options.search_engine && !options.extensions) {
    return read;
  }
  std::optional<base::DictValue> prefs = ReadJsonDict(dir->Append("Preferences"));
  if (options.search_engine && prefs) {
    read.search_engine = ReadDefaultSearchEngine(*prefs);
  }
  if (options.extensions) {
    // Chromium keeps extension settings in Secure Preferences on macOS, in Preferences elsewhere.
    for (const char* file : {"Secure Preferences", "Preferences"}) {
      std::optional<base::DictValue> p =
          file == std::string_view("Preferences") ? std::move(prefs) : ReadJsonDict(dir->Append(file));
      if (p) {
        ReadExtensions(*dir, *p, read.extensions);
      }
    }
  }
  return read;
}

// A source profile's Login Data and Cookies, read from copies (the other browser may be running and hold them open;
// its files are only read).
ProfileSecrets ReadSidebarSecrets(const base::FilePath& root,
                                  const std::string& key,
                                  SafeStorageKey aes_key,
                                  SidebarImportOptions options,
                                  int64_t now_ms,
                                  const base::FilePath& temp_root) {
  const std::optional<base::FilePath> profile = SidebarProfileDir(root, key);
  if (!profile) {
    return ProfileSecrets();
  }
  return ReadProfileSecrets(*profile, aes_key, options.passwords, options.cookies, ToChromeTime(now_ms), temp_root);
}

base::Time FromChromeUs(int64_t us) {
  return us > 0 ? base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(us)) : base::Time();
}

net::CookieSameSite SameSiteOf(int v) {
  switch (v) {
    case 0:
      return net::CookieSameSite::NO_RESTRICTION;
    case 1:
      return net::CookieSameSite::LAX_MODE;
    case 2:
      return net::CookieSameSite::STRICT_MODE;
    default:
      return net::CookieSameSite::UNSPECIFIED;
  }
}

net::CookiePriority PriorityOf(int v) {
  return v == 0 ? net::COOKIE_PRIORITY_LOW : v == 2 ? net::COOKIE_PRIORITY_HIGH : net::COOKIE_PRIORITY_MEDIUM;
}

password_manager::StoredCredential ToCredential(SidebarLogin& login) {
  password_manager::StoredCredential form;
  form.scheme = password_manager::PasswordForm::Scheme::kHtml;
  form.signon_realm = login.signon_realm;
  form.url = GURL(login.origin_url);
  form.action = GURL(login.action_url);
  form.username_element = base::UTF8ToUTF16(login.username_element);
  form.password_element = base::UTF8ToUTF16(login.password_element);
  // PasswordStore::AddLogins CHECKs that a never-save form has no username or password.
  if (!login.blocked_by_user) {
    form.username_value = base::UTF8ToUTF16(login.username);
    form.password_value = password_manager::PasswordString(base::UTF8ToUTF16(login.password));
  }
  OPENSSL_cleanse(login.password.data(), login.password.size());
  login.password.clear();
  form.date_created = FromChromeUs(login.date_created);
  form.date_password_modified = form.date_created;
  form.blocked_by_user = login.blocked_by_user;
  form.type = password_manager::PasswordForm::Type::kImported;
  form.in_store = password_manager::PasswordForm::Store::kProfileStore;
  return form;
}

// Writes the logins to the profile's password store and clears their plaintext; how many went in. The store merges a
// login it already has (same realm, url, username and fields), so a re-import adds nothing.
int AddLogins(Profile* profile, std::vector<SidebarLogin>& logins) {
  if (logins.empty()) {
    return 0;
  }
  scoped_refptr<password_manager::PasswordStoreInterface> store =
      ProfilePasswordStoreFactory::GetForProfile(profile, ServiceAccessType::EXPLICIT_ACCESS);
  std::vector<password_manager::StoredCredential> forms;
  forms.reserve(logins.size());
  for (SidebarLogin& login : logins) {
    forms.push_back(ToCredential(login));
  }
  if (!store) {
    return 0;
  }
  const int added = static_cast<int>(forms.size());
  store->AddLogins(std::move(forms));
  return added;
}

network::mojom::CookieManager* g_cookie_manager_for_testing = nullptr;

// Sets the cookies in the profile's cookie jar; `done` gets how many were set and how many were junk or refused.
void SetCookies(Profile* profile,
                const std::vector<SidebarCookie>& rows,
                base::OnceCallback<void(int set, int bad)> done) {
  int bad = 0;
  std::vector<std::pair<std::unique_ptr<net::CanonicalCookie>, GURL>> valid;
  for (const SidebarCookie& c : rows) {
    // A partitioned cookie keeps its partition; a partition that doesn't parse is junk.
    std::optional<net::CookiePartitionKey> partition;
    if (!c.top_frame_site_key.empty()) {
      base::expected<std::optional<net::CookiePartitionKey>, std::string> key =
          net::CookiePartitionKey::FromStorage(c.top_frame_site_key, c.has_cross_site_ancestor);
      if (!key.has_value() || !key->has_value()) {
        ++bad;
        continue;
      }
      partition = std::move(key).value();
    }
    std::unique_ptr<net::CanonicalCookie> cookie = net::CanonicalCookie::FromStorage(
        c.name, c.value, c.host_key, c.path.empty() ? "/" : c.path, FromChromeUs(c.creation_utc),
        c.persistent ? FromChromeUs(c.expires_utc) : base::Time(), FromChromeUs(c.last_access_utc),
        FromChromeUs(c.creation_utc), c.secure, c.httponly, SameSiteOf(c.samesite), PriorityOf(c.priority),
        std::move(partition),
        c.secure ? net::CookieSourceScheme::kSecure : net::CookieSourceScheme::kNonSecure,
        url::PORT_UNSPECIFIED, net::CookieSourceType::kOther,
        net::CanonicalCookieFromStorageCallSite::kCookieManager);
    if (!cookie) {
      ++bad;
      continue;
    }
    // The URL the cookie is set for: its host without the domain dot, on the scheme it needs.
    GURL source(base::StrCat({c.secure ? "https://" : "http://",
                              base::TrimString(c.host_key, ".", base::TRIM_LEADING), cookie->Path()}));
    valid.emplace_back(std::move(cookie), std::move(source));
  }
  base::RepeatingCallback<void(bool)> each = base::BarrierCallback<bool>(
      valid.size(), base::BindOnce(
                        [](base::OnceCallback<void(int, int)> done, int bad, std::vector<bool> results) {
                          const int set = static_cast<int>(std::count(results.begin(), results.end(), true));
                          std::move(done).Run(set, bad + static_cast<int>(results.size()) - set);
                        },
                        std::move(done), bad));
  if (valid.empty()) {
    return;
  }
  network::mojom::CookieManager* cookies =
      g_cookie_manager_for_testing
          ? g_cookie_manager_for_testing
          : profile->GetDefaultStoragePartition()->GetCookieManagerForBrowserProcess();
  const net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
  // A reply the pipe drops (the network service restarting) counts as refused, so the barrier always finishes.
  net::CookieInclusionStatus dropped;
  dropped.AddExclusionReason(net::CookieInclusionStatus::ExclusionReason::EXCLUDE_UNKNOWN_ERROR);
  for (const auto& [cookie, source] : valid) {
    cookies->SetCanonicalCookie(
        *cookie, source, options,
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce([](base::RepeatingCallback<void(bool)> each,
                              net::CookieAccessResult access) { each.Run(access.status.IsInclude()); },
                           each),
            net::CookieAccessResult(dropped)));
  }
}

}  // namespace

// ChromeJob --------------------------------------------------------------------

// One Chrome import: resolve the profile directory, load the target space's profile, then bookmarks, then history,
// then the saved passwords and cookies.
// A source profile's site data, read off the UI thread: Chrome's and each sidebar profile's folder.
SiteDataRead ReadSidebarSiteData(const base::FilePath& root,
                                 const std::string& key,
                                 const base::FilePath& target_profile,
                                 SiteDataOptions options,
                                 const base::FilePath& temp_root) {
  const std::optional<base::FilePath> profile = SidebarProfileDir(root, key);
  return profile ? ReadSiteData(*profile, target_profile, options, temp_root) : SiteDataRead();
}

std::optional<ContentSettingsType> PermissionType(std::string_view name) {
  if (name == "notifications") {
    return ContentSettingsType::NOTIFICATIONS;
  }
  if (name == "media_stream_camera") {
    return ContentSettingsType::MEDIASTREAM_CAMERA;
  }
  if (name == "media_stream_mic") {
    return ContentSettingsType::MEDIASTREAM_MIC;
  }
  if (name == "geolocation") {
    return ContentSettingsType::GEOLOCATION;
  }
  return std::nullopt;
}

// The source's allowed and blocked sites become the Rio profile's; a site Rio already decided for keeps Rio's choice.
int SetPermissions(Profile* profile, const std::vector<ImportedPermission>& rows, int* skipped) {
  HostContentSettingsMap* map = HostContentSettingsMapFactory::GetForProfile(profile);
  if (!map) {
    return 0;
  }
  int set = 0;
  for (const ImportedPermission& row : rows) {
    const std::optional<ContentSettingsType> type = PermissionType(row.type);
    const ContentSettingsPattern pattern = ContentSettingsPattern::FromString(row.pattern);
    if (!type || !pattern.IsValid() || pattern == ContentSettingsPattern::Wildcard()) {
      ++*skipped;
      continue;
    }
    const GURL site = GURL(row.pattern);
    if (site.is_valid() && map->GetContentSetting(site, site, *type) != map->GetDefaultContentSetting(*type)) {
      continue;
    }
    map->SetContentSettingCustomScope(pattern, ContentSettingsPattern::Wildcard(), *type,
                                      row.setting == 1 ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
    ++set;
  }
  return set;
}

// Saved addresses into the Rio profile's Autofill; an address Rio already has (same fields) isn't added twice.
int AddAddresses(Profile* profile, const std::vector<ImportedAddress>& rows) {
  autofill::PersonalDataManager* pdm = autofill::PersonalDataManagerFactory::GetForBrowserContext(profile);
  if (!pdm) {
    return 0;
  }
  autofill::AddressDataManager& addresses = pdm->address_data_manager();
  int added = 0;
  for (const ImportedAddress& row : rows) {
    autofill::AutofillProfile address{autofill::AddressCountryCode(row.country)};
    for (const auto& [type, value] : row.fields) {
      address.SetRawInfoWithVerificationStatus(static_cast<autofill::FieldType>(type), base::UTF8ToUTF16(value),
                                               autofill::VerificationStatus::kUserVerified);
    }
    address.FinalizeAfterImport();
    const bool known = std::ranges::any_of(addresses.GetProfiles(), [&](const autofill::AutofillProfile* p) {
      return p->Compare(address) == 0;
    });
    if (!known) {
      addresses.AddProfile(address);
      ++added;
    }
  }
  return added;
}

// The source's engine becomes the Rio profile's default: the matching engine Rio already has, or a new one. Its name,
// or nullopt when Rio can't make it the default. `urls` is loaded.
std::optional<std::string> ApplySearchEngine(TemplateURLService* urls, const ImportedSearchEngine& engine) {
  TemplateURL* url = urls->GetTemplateURLForKeyword(base::UTF8ToUTF16(engine.keyword));
  if (!url) {
    TemplateURLData data;
    data.SetShortName(base::UTF8ToUTF16(engine.name));
    data.SetKeyword(base::UTF8ToUTF16(engine.keyword));
    data.SetURL(engine.url);
    url = urls->Add(std::make_unique<TemplateURL>(data));
  }
  if (!url || !urls->CanMakeDefault(url)) {
    return std::nullopt;
  }
  urls->SetUserSelectedDefaultSearchProvider(url);
  return engine.name;
}

class CluImportService::ChromeJob {
 public:
  ChromeJob(CluImportService* service,
            ChromeImportRequest request,
            std::string profile_id,
            ChromeCallback done)
      : service_(service),
        request_(std::move(request)),
        profile_id_(std::move(profile_id)),
        done_(std::move(done)) {}
  ChromeJob(const ChromeJob&) = delete;
  ChromeJob& operator=(const ChromeJob&) = delete;
  ~ChromeJob() = default;

  void Start() {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking,
        base::BindOnce(&ReadBookmarks, service_->Root(), request_.profile_dir,
                       request_.bookmarks),
        base::BindOnce(&ChromeJob::OnRead, weak_factory_.GetWeakPtr()));
  }

 private:
  void OnRead(BookmarksRead read) {
    if (!read.found) {
      LOG(WARNING) << "clu: import " << kNoProfile;
      Answer(base::unexpected(ImportStartError::kNotFound));
      return;
    }
    dir_ = read.dir;
    bookmarks_ = std::move(read.parsed);
    service_->SetProgress(
        ImportProgress{.source = "chrome", .phase = "bookmarks"});
    CluSpacesService::Get()->LoadProfile(
        profile_id_,
        base::BindOnce(&ChromeJob::OnProfile, weak_factory_.GetWeakPtr()));
  }

  void OnProfile(Profile* profile) {
    CluModelService* model = profile ? CluModelService::Get(profile) : nullptr;
    if (!model) {
      Stop(kCannotFinish);
      return;
    }
    profile_ = profile->GetWeakPtr();
    model_ = model->GetWeakPtr();
    model->WhenLoaded(
        base::BindOnce(&ChromeJob::ImportBookmarks, weak_factory_.GetWeakPtr()));
  }

  void ImportBookmarks() {
    if (!request_.bookmarks) {
      ReadHistory();
      return;
    }
    if (bookmarks_.error) {
      Stop(*bookmarks_.error);
      return;
    }
    result_.skipped += bookmarks_.skipped;
    if (bookmarks_.nodes.empty() || !model_) {
      ReadHistory();
      return;
    }
    model_->GetImportMap(
        kChromeSource, KeysOf(bookmarks_.nodes),
        base::BindOnce(&ChromeJob::OnBookmarkMap, weak_factory_.GetWeakPtr()));
  }

  void OnBookmarkMap(std::map<std::string, std::string> import_map) {
    if (!model_ || !model_->FindSpace(request_.target_space_id)) {
      Stop(kCannotFinish);
      return;
    }
    ImportRows rows = PlanPinnedRows(
        bookmarks_.nodes, request_.target_space_id, import_map, model_->tree(),
        base::BindRepeating(&NewId), service_->NowMs());
    model_->AddImportedRows(std::move(rows.rows));
    model_->PutImportMap(kChromeSource, std::move(rows.mapped));
    result_.bookmarks = rows.tabs;
    service_->SetProgress(ImportProgress{
        .source = "chrome",
        .phase = "bookmarks",
        .done = static_cast<int>(bookmarks_.nodes.size()),
        .total = static_cast<int>(bookmarks_.nodes.size()),
        .skipped = result_.skipped});
    ReadHistory();
  }

  void ReadHistory() {
    if (!request_.history) {
      AskKeychain();
      return;
    }
    service_->SetProgress(ImportProgress{
        .source = "chrome", .phase = "history", .skipped = result_.skipped});
    HistoryRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadChromeHistory, dir_.Append("History"),
                       service_->NowMs(), TempRoot()),
        base::BindOnce(&ChromeJob::OnHistory, weak_factory_.GetWeakPtr()));
  }

  void OnHistory(HistoryReadResult read) {
    if (read.error) {
      Stop(*read.error);
      return;
    }
    if (!model_) {
      Stop(kCannotFinish);
      return;
    }
    result_.skipped += read.skipped;
    std::vector<std::string> keys;
    keys.reserve(read.rows.size());
    for (const ImportedVisit& row : read.rows) {
      keys.push_back(base::StrCat({request_.profile_dir, "|", row.url, "|",
                                   base::NumberToString(row.last_visit_at)}));
    }
    visits_ = std::move(read.rows);
    model_->GetImportMap(
        kChromeHistorySource, keys,
        base::BindOnce(&ChromeJob::OnHistoryMap, weak_factory_.GetWeakPtr(),
                       keys));
  }

  void OnHistoryMap(std::vector<std::string> keys,
                    std::map<std::string, std::string> import_map) {
    history::HistoryService* history =
        profile_ ? HistoryServiceFactory::GetForProfile(
                       profile_.get(), ServiceAccessType::EXPLICIT_ACCESS)
                 : nullptr;
    if (!history || !model_) {
      Stop(kCannotFinish);
      return;
    }
    history::URLRows rows;
    std::vector<std::pair<std::string, std::string>> mapped;
    for (size_t i = 0; i < visits_.size(); ++i) {
      if (import_map.contains(keys[i])) {
        continue;
      }
      const ImportedVisit& v = visits_[i];
      history::URLRow row{GURL(v.url)};
      row.set_title(base::UTF8ToUTF16(v.title));
      row.set_visit_count(v.visit_count);
      row.set_typed_count(v.typed_count);
      row.set_last_visit(base::Time::FromMillisecondsSinceUnixEpoch(v.last_visit_at));
      row.set_hidden(false);
      rows.push_back(std::move(row));
      mapped.emplace_back(keys[i], v.url);
    }
    if (!rows.empty()) {
      // Chromium has no "imported from Chrome" source; OS_MIGRATION_IMPORTED is the one for visits brought over from
      // another installation, so imported visits never count as browsed (sync, typed scores).
      history->AddPagesWithDetails(rows, history::SOURCE_OS_MIGRATION_IMPORTED);
    }
    result_.history = static_cast<int>(rows.size());
    model_->PutImportMap(kChromeHistorySource, std::move(mapped));
    AskKeychain();
  }

  // The keychain secret for the passwords and cookies; macOS shows its own Allow prompt, which blocks this read.
  void AskKeychain() {
    if (!request_.passwords && !request_.cookies) {
      ReadSite();
      return;
    }
    service_->SetProgress(ImportProgress{
        .source = "chrome", .phase = "keychain", .skipped = result_.skipped});
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking,
        service_->SecretSource(kChromeSafeStorageService, kChromeSafeStorageAccount),
        base::BindOnce(&ChromeJob::OnSecret, weak_factory_.GetWeakPtr()));
  }

  void OnSecret(std::optional<std::string> secret) {
    const std::optional<SafeStorageKey> key =
        secret ? DeriveSafeStorageKey(*secret) : std::nullopt;
    if (secret) {
      OPENSSL_cleanse(secret->data(), secret->size());
    }
    if (!key) {
      // Denied (or no key): what came over stays, and the result says the passwords and sign-ins were skipped. Site
      // storage, permissions and addresses aren't encrypted, so they still come over.
      result_.secrets_denied = true;
      ReadSite();
      return;
    }
    service_->SetProgress(ImportProgress{
        .source = "chrome", .phase = "secrets", .skipped = result_.skipped});
    HistoryRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadProfileSecrets, dir_, *key, request_.passwords,
                       request_.cookies, ToChromeTime(service_->NowMs()),
                       TempRoot()),
        base::BindOnce(&ChromeJob::OnSecrets, weak_factory_.GetWeakPtr()));
  }

  void OnSecrets(ProfileSecrets read) {
    if (!profile_) {
      Stop(kCannotFinish);
      return;
    }
    result_.skipped +=
        read.skips.undecryptable + read.skips.bad_rows + read.skips.over_cap;
    result_.passwords = AddLogins(profile_.get(), read.logins);
    SetCookies(profile_.get(), read.cookies,
               base::BindOnce(&ChromeJob::OnCookiesSet,
                              weak_factory_.GetWeakPtr()));
  }

  void OnCookiesSet(int set, int bad) {
    result_.cookies = set;
    result_.skipped += bad;
    ReadSite();
  }

  // Site storage and permissions come with the sign-ins (the cookies checkbox); addresses and the default search
  // engine with their own checkboxes.
  void ReadSite() {
    if (!profile_) {
      Stop(kCannotFinish);
      return;
    }
    service_->SetProgress(ImportProgress{
        .source = "chrome", .phase = "site data", .skipped = result_.skipped});
    const SiteDataOptions options{.storage = request_.cookies,
                                  .permissions = request_.cookies,
                                  .addresses = request_.autofill,
                                  .search_engine = request_.search_engine};
    HistoryRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadSiteData, dir_, profile_->GetPath(), options,
                       TempRoot()),
        base::BindOnce(&ChromeJob::OnSiteData, weak_factory_.GetWeakPtr()));
  }

  void OnSiteData(SiteDataRead read) {
    if (!profile_) {
      Stop(kCannotFinish);
      return;
    }
    result_.skipped += read.skipped;
    result_.site_storage = read.storage.sites();
    result_.permissions =
        SetPermissions(profile_.get(), read.permissions, &result_.skipped);
    result_.addresses = AddAddresses(profile_.get(), read.addresses);
    TemplateURLService* urls =
        read.search_engine
            ? TemplateURLServiceFactory::GetForProfile(profile_.get())
            : nullptr;
    if (!urls) {
      Answer(result_);
      return;
    }
    search_engine_ = std::move(read.search_engine);
    if (urls->loaded()) {
      OnSearchEnginesLoaded();
      return;
    }
    search_loaded_ = urls->RegisterOnLoadedCallback(base::BindOnce(
        &ChromeJob::OnSearchEnginesLoaded, weak_factory_.GetWeakPtr()));
    urls->Load();
  }

  void OnSearchEnginesLoaded() {
    TemplateURLService* urls =
        profile_ ? TemplateURLServiceFactory::GetForProfile(profile_.get())
                 : nullptr;
    if (urls && urls->loaded() && search_engine_) {
      result_.search_engine = ApplySearchEngine(urls, *search_engine_);
    }
    search_engine_.reset();
    Answer(result_);
  }

  void Stop(const std::string& error) {
    LOG(WARNING) << "clu: chrome import stopped: " << error;
    result_.error = error;
    Answer(result_);
  }

  void Answer(base::expected<ChromeImportResult, ImportStartError> result) {
    ChromeCallback done = std::move(done_);
    CluImportService* service = service_;
    // Deletes this job.
    service->Finish();
    std::move(done).Run(std::move(result));
  }

  const raw_ptr<CluImportService> service_;
  const ChromeImportRequest request_;
  const std::string profile_id_;
  ChromeCallback done_;
  base::FilePath dir_;
  BookmarksResult bookmarks_;
  std::vector<ImportedVisit> visits_;
  base::WeakPtr<Profile> profile_;
  base::WeakPtr<CluModelService> model_;
  std::optional<ImportedSearchEngine> search_engine_;
  base::CallbackListSubscription search_loaded_;
  ChromeImportResult result_;
  base::WeakPtrFactory<ChromeJob> weak_factory_{this};
};

// SidebarJob -------------------------------------------------------------------

// One sidebar-file import: read and check the plan, then for each source profile make or find its Clu profile, load
// it, add its favorites, then make or find each of its spaces with their themes, pinned trees and Today tabs, then
// bring the source profile's history over from its Chromium History file.
class CluImportService::SidebarJob {
 public:
  SidebarJob(CluImportService* service, SidebarImportOptions options, SidebarCallback done)
      : service_(service), options_(options), done_(std::move(done)) {}
  SidebarJob(const SidebarJob&) = delete;
  SidebarJob& operator=(const SidebarJob&) = delete;
  ~SidebarJob() = default;

  void Start() {
    service_->SetProgress(ImportProgress{.source = "arc", .phase = "reading"});
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking, base::BindOnce(&ReadSidebar, service_->Root()),
        base::BindOnce(&SidebarJob::OnRead, weak_factory_.GetWeakPtr()));
  }

 private:
  void OnRead(SidebarRead read) {
    if (!read.found) {
      Answer(base::unexpected(ImportStartError::kNotFound));
      return;
    }
    plan_ = std::move(read.plan);
    if (plan_.error) {
      // Nothing is written; the counts stay 0.
      result_.error = plan_.error;
      Answer(result_);
      return;
    }
    result_.skipped = plan_.skipped;
    CluSpacesService* spaces = CluSpacesService::Get();
    const CluSpaceList& list = spaces->list();
    // The caps first, so a file too big for Clu writes nothing.
    size_t new_profiles = 0;
    std::set<std::string> new_profile_keys;
    for (const SidebarProfilePlan& p : plan_.profiles) {
      const std::optional<std::string> id = MappedId("profile:" + p.key);
      if (!id || !list.FindProfile(*id)) {
        ++new_profiles;
        new_profile_keys.insert(p.key);
      }
    }
    // NextSpace() reuses a mapped space only while it is in its imported profile; one the user moved to another
    // profile is made again, so it counts as new here too.
    size_t new_spaces = 0;
    for (const SidebarSpacePlan& s : plan_.spaces) {
      const std::optional<std::string> id = MappedId(s.key);
      const SpaceEntry* space = id ? list.FindSpace(*id) : nullptr;
      const std::optional<std::string> profile_id =
          MappedId("profile:" + s.profile_key);
      if (!space || new_profile_keys.contains(s.profile_key) ||
          space->profile_id != profile_id) {
        ++new_spaces;
      }
    }
    if (list.spaces().size() + new_spaces > kMaxSpaces) {
      Stop(kTooManySpaces);
      return;
    }
    if (list.profiles().size() + new_profiles > kMaxProfiles) {
      Stop(kTooManyProfiles);
      return;
    }
    for (const SidebarSpacePlan& s : plan_.spaces) {
      total_ += static_cast<int>(s.pinned.size() + s.today.size());
    }
    for (const SidebarProfilePlan& p : plan_.profiles) {
      total_ += static_cast<int>(p.favorites.size());
    }
    Progress();
    NextProfile();
  }

  void NextProfile() {
    if (profile_index_ >= plan_.profiles.size()) {
      Answer(result_);
      return;
    }
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    CluSpacesService* spaces = CluSpacesService::Get();
    const std::optional<std::string> mapped = MappedId("profile:" + p.key);
    if (mapped && spaces->list().FindProfile(*mapped)) {
      OnProfileId(*mapped, /*created=*/false);
      return;
    }
    spaces->CreateProfile(
        SourceProfileName(p), base::BindOnce(
                  [](base::WeakPtr<SidebarJob> job,
                     base::expected<std::string, SpacesError> id) {
                    if (!job) {
                      return;
                    }
                    if (!id.has_value()) {
                      job->Stop(id.error() == SpacesError::kMaxProfiles
                                    ? kTooManyProfiles
                                    : kCannotFinish);
                      return;
                    }
                    job->OnProfileId(id.value(), /*created=*/true);
                  },
                  weak_factory_.GetWeakPtr()));
  }

  // The source profile's own name, else its first space's name (never "Imported N"); none gives Clu's
  // default.
  std::optional<std::string> SourceProfileName(const SidebarProfilePlan& p) const {
    if (!p.name.empty()) {
      return p.name;
    }
    for (const SidebarSpacePlan& s : plan_.spaces) {
      if (s.profile_key == p.key && !s.name.empty()) {
        return s.name;
      }
    }
    return std::nullopt;
  }

  void OnProfileId(const std::string& profile_id, bool created) {
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    if (created) {
      Remember("profile:" + p.key, profile_id);
      ++result_.profiles;
    }
    profile_id_ = profile_id;
    CluSpacesService::Get()->LoadProfile(
        profile_id, base::BindOnce(&SidebarJob::OnProfile,
                                   weak_factory_.GetWeakPtr()));
  }

  void OnProfile(Profile* profile) {
    CluModelService* model = profile ? CluModelService::Get(profile) : nullptr;
    if (!model) {
      Stop(kCannotFinish);
      return;
    }
    profile_ = profile->GetWeakPtr();
    model_ = model->GetWeakPtr();
    model->WhenLoaded(
        base::BindOnce(&SidebarJob::ImportFavorites, weak_factory_.GetWeakPtr()));
  }

  void ImportFavorites() {
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    if (!model_) {
      Stop(kCannotFinish);
      return;
    }
    model_->GetImportMap(
        kSidebarSource, KeysOf(p.favorites),
        base::BindOnce(&SidebarJob::OnFavoritesMap, weak_factory_.GetWeakPtr()));
  }

  void OnFavoritesMap(std::map<std::string, std::string> import_map) {
    if (!model_) {
      Stop(kCannotFinish);
      return;
    }
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    if (options_.favorites) {
      ImportRows rows =
          PlanFavoriteRows(p.favorites, import_map, model_->tree(),
                           base::BindRepeating(&NewId), service_->NowMs());
      model_->AddImportedRows(std::move(rows.rows));
      model_->PutImportMap(kSidebarSource, std::move(rows.mapped));
      result_.favorites += rows.tabs;
      result_.skipped.duplicates += rows.duplicates;
      result_.skipped.over_cap += rows.over_cap;
    }
    done_count_ += static_cast<int>(p.favorites.size());
    Progress();
    space_index_ = 0;
    first_space_id_.clear();
    NextSpace();
  }

  void NextSpace() {
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    while (space_index_ < plan_.spaces.size() &&
           plan_.spaces[space_index_].profile_key != p.key) {
      ++space_index_;
    }
    if (space_index_ >= plan_.spaces.size()) {
      ReadHistory();
      return;
    }
    const SidebarSpacePlan& s = plan_.spaces[space_index_];
    CluSpacesService* spaces = CluSpacesService::Get();
    std::string space_id;
    const std::optional<std::string> mapped = MappedId(s.key);
    const SpaceEntry* kept = mapped ? spaces->list().FindSpace(*mapped) : nullptr;
    if (kept && kept->profile_id == profile_id_) {
      space_id = kept->id;
    } else {
      const std::optional<std::string> name =
          s.name.empty() ? std::nullopt : std::optional<std::string>(s.name);
      const std::optional<std::string> color(
          ImportSpaceColor(spaces->list().spaces().size()));
      std::optional<SpaceIconInput> icon;
      if (s.emoji) {
        icon = SpaceIconInput{.emoji = true, .value = *s.emoji};
      } else if (s.symbol) {
        icon = SpaceIconInput{.value = *s.symbol, .symbol = true};
      }
      base::expected<std::string, SpacesError> created =
          spaces->CreateSpace(name, icon, color, profile_id_);
      if (!created.has_value() && icon) {
        // An emoji or symbol Clu's rules refuse becomes the letter icon.
        created = spaces->CreateSpace(name, std::nullopt, color, profile_id_);
      }
      if (!created.has_value()) {
        Stop(created.error() == SpacesError::kMaxSpaces ? kTooManySpaces
                                                        : kCannotFinish);
        return;
      }
      space_id = created.value();
      Remember(s.key, space_id);
      ++result_.spaces;
      if (s.theme) {
        if (CluAppSettings* settings = CluAppSettings::Get()) {
          settings->SetSpaceTheme(space_id, *s.theme);
        }
      }
    }
    if (first_space_id_.empty()) {
      first_space_id_ = space_id;
    }
    if (result_.main_space_id.empty()) {
      result_.main_space_id = space_id;
    }
    if (!model_) {
      Stop(kCannotFinish);
      return;
    }
    model_->GetImportMap(
        kSidebarSource, KeysOf(s.pinned),
        base::BindOnce(&SidebarJob::OnPinnedMap, weak_factory_.GetWeakPtr(),
                       space_id));
  }

  void OnPinnedMap(std::string space_id,
                   std::map<std::string, std::string> import_map) {
    if (!model_ || !model_->FindSpace(space_id)) {
      Stop(kCannotFinish);
      return;
    }
    const SidebarSpacePlan& s = plan_.spaces[space_index_];
    if (options_.pinned) {
      ImportRows rows = PlanPinnedRows(s.pinned, space_id, import_map,
                                       model_->tree(), base::BindRepeating(&NewId),
                                       service_->NowMs());
      KeepSpaceRows(import_map, rows.mapped);
      model_->AddImportedRows(std::move(rows.rows));
      model_->PutImportMap(kSidebarSource, std::move(rows.mapped));
      result_.pinned += rows.tabs;
    }
    done_count_ += static_cast<int>(s.pinned.size());
    Progress();
    model_->GetImportMap(
        kSidebarSource, KeysOf(s.today),
        base::BindOnce(&SidebarJob::OnTodayMap, weak_factory_.GetWeakPtr(),
                       std::move(space_id)));
  }

  void OnTodayMap(std::string space_id,
                  std::map<std::string, std::string> import_map) {
    if (!model_ || !model_->FindSpace(space_id)) {
      Stop(kCannotFinish);
      return;
    }
    const SidebarSpacePlan& s = plan_.spaces[space_index_];
    if (options_.today) {
      ImportRows rows = PlanTodayRows(s.today, space_id, import_map,
                                      model_->tree(), base::BindRepeating(&NewId),
                                      service_->NowMs());
      KeepSpaceRows(import_map, rows.mapped);
      model_->AddImportedRows(std::move(rows.rows));
      model_->PutImportMap(kSidebarSource, std::move(rows.mapped));
      result_.today += rows.tabs;
    }
    ImportSplits(s);
    done_count_ += static_cast<int>(s.today.size());
    Progress();
    ++space_index_;
    NextSpace();
  }

  // The rows the space's pinned and Today keys stand for, old (a run before) and new.
  void KeepSpaceRows(const std::map<std::string, std::string>& import_map,
                     const std::vector<std::pair<std::string, std::string>>& added) {
    space_rows_.insert(import_map.begin(), import_map.end());
    space_rows_.insert(added.begin(), added.end());
  }

  // The space's split views become Clu splits of their rows (a split in a folder
  // would otherwise arrive as flat tabs). A pane that didn't come over, or is in a split already (a run before made it), leaves that split out.
  void ImportSplits(const SidebarSpacePlan& s) {
    for (const SidebarSplitPlan& split : s.splits) {
      std::vector<std::string> ids;
      for (const std::string& key : split.keys) {
        const auto it = space_rows_.find(key);
        if (it == space_rows_.end() || model_->SplitOf(it->second)) {
          ids.clear();
          break;
        }
        ids.push_back(it->second);
      }
      if (ids.size() < 2) {
        continue;
      }
      ids.resize(std::min(ids.size(), kMaxSplitPanes));
      std::ignore = model_->CreateSplit(
          ids, split.stacked ? SplitDir::kCol : SplitDir::kRow, ids[0]);
    }
    space_rows_.clear();
  }

  // The source profile's History file, read with Chrome's reader (it is the same Chromium format). A profile with no
  // file has no history to bring; a file that can't be read is counted and the import goes on.
  void ReadHistory() {
    if (!options_.history) {
      NextProfileAfterHistory();
      return;
    }
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    service_->SetProgress(ImportProgress{.source = "arc",
                                         .phase = "history",
                                         .done = done_count_,
                                         .total = total_,
                                         .skipped = result_.skipped.total()});
    HistoryRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadSidebarHistory, service_->Root(), p.key,
                       service_->NowMs(), TempRoot()),
        base::BindOnce(&SidebarJob::OnHistory, weak_factory_.GetWeakPtr()));
  }

  void OnHistory(std::optional<HistoryReadResult> found) {
    if (!found) {
      NextProfileAfterHistory();
      return;
    }
    HistoryReadResult read = std::move(*found);
    if (read.error) {
      ++result_.skipped.history_files;
      NextProfileAfterHistory();
      return;
    }
    if (!model_) {
      Stop(kCannotFinish);
      return;
    }
    result_.skipped.bad_urls += read.skipped;
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    std::vector<std::string> keys;
    keys.reserve(read.rows.size());
    for (const ImportedVisit& row : read.rows) {
      keys.push_back(base::StrCat({p.key, "|", row.url, "|",
                                   base::NumberToString(row.last_visit_at)}));
    }
    visits_ = std::move(read.rows);
    model_->GetImportMap(
        kSidebarHistorySource, keys,
        base::BindOnce(&SidebarJob::OnHistoryMap, weak_factory_.GetWeakPtr(),
                       keys));
  }

  void OnHistoryMap(std::vector<std::string> keys,
                    std::map<std::string, std::string> import_map) {
    history::HistoryService* history =
        profile_ ? HistoryServiceFactory::GetForProfile(
                       profile_.get(), ServiceAccessType::EXPLICIT_ACCESS)
                 : nullptr;
    if (!history || !model_) {
      Stop(kCannotFinish);
      return;
    }
    history::URLRows rows;
    std::vector<std::pair<std::string, std::string>> mapped;
    for (size_t i = 0; i < visits_.size(); ++i) {
      if (import_map.contains(keys[i])) {
        continue;
      }
      const ImportedVisit& v = visits_[i];
      history::URLRow row{GURL(v.url)};
      row.set_title(base::UTF8ToUTF16(v.title));
      row.set_visit_count(v.visit_count);
      row.set_typed_count(v.typed_count);
      row.set_last_visit(
          base::Time::FromMillisecondsSinceUnixEpoch(v.last_visit_at));
      row.set_hidden(false);
      rows.push_back(std::move(row));
      mapped.emplace_back(keys[i], v.url);
    }
    if (!rows.empty()) {
      // As for Chrome's history: visits brought over from another installation never count as browsed here.
      history->AddPagesWithDetails(rows, history::SOURCE_OS_MIGRATION_IMPORTED);
    }
    result_.history += static_cast<int>(rows.size());
    model_->PutImportMap(kSidebarHistorySource, std::move(mapped));
    visits_.clear();
    NextProfileAfterHistory();
  }

  // Bookmarks, the default search engine and the extension list, between history and the keychain.
  void NextProfileAfterHistory() {
    if (!options_.bookmarks && !options_.search_engine && !options_.extensions) {
      NextProfileAfterExtras();
      return;
    }
    service_->SetProgress(ImportProgress{.source = "arc",
                                         .phase = "extras",
                                         .done = done_count_,
                                         .total = total_,
                                         .skipped = result_.skipped.total()});
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking,
        base::BindOnce(&ReadSidebarExtras, service_->Root(),
                       plan_.profiles[profile_index_].key, options_),
        base::BindOnce(&SidebarJob::OnExtras, weak_factory_.GetWeakPtr()));
  }

  void OnExtras(SidebarExtrasRead read) {
    for (SidebarExtension& e : read.extensions) {
      if (result_.extensions.size() < kMaxExtensions &&
          std::none_of(result_.extensions.begin(), result_.extensions.end(),
                       [&](const SidebarExtension& x) { return x.id == e.id; })) {
        e.profile_id = profile_id_;
        result_.extensions.push_back(std::move(e));
      }
    }
    result_.skipped.bad_urls += read.bookmarks.skipped;
    bookmarks_ = std::move(read.bookmarks.nodes);
    TemplateURLService* urls =
        read.search_engine && profile_
            ? TemplateURLServiceFactory::GetForProfile(profile_.get())
            : nullptr;
    if (urls && !urls->loaded()) {
      // The engine is set once the profile's search engines load, before the import goes on.
      search_engine_ = std::move(read.search_engine);
      search_loaded_ = urls->RegisterOnLoadedCallback(base::BindOnce(
          &SidebarJob::OnSearchEnginesLoaded, weak_factory_.GetWeakPtr()));
      urls->Load();
      return;
    }
    if (urls) {
      search_engine_ = std::move(read.search_engine);
    }
    OnSearchEnginesLoaded();
  }

  void OnSearchEnginesLoaded() {
    SetSearchEngine();
    if (bookmarks_.empty() || first_space_id_.empty() || !model_) {
      bookmarks_.clear();
      NextProfileAfterExtras();
      return;
    }
    model_->GetImportMap(
        kSidebarSource, KeysOf(bookmarks_),
        base::BindOnce(&SidebarJob::OnBookmarkMap, weak_factory_.GetWeakPtr()));
  }

  // The source's engine becomes the Rio profile's default: the matching engine Rio already has, or a new one.
  void SetSearchEngine() {
    TemplateURLService* urls =
        profile_ ? TemplateURLServiceFactory::GetForProfile(profile_.get()) : nullptr;
    if (!search_engine_ || !urls || !urls->loaded()) {
      return;
    }
    const SidebarSearchEngine engine = std::move(*search_engine_);
    search_engine_.reset();
    if (std::optional<std::string> name = ApplySearchEngine(urls, engine)) {
      result_.search_engine = std::move(name);
    }
  }

  void OnBookmarkMap(std::map<std::string, std::string> import_map) {
    if (!model_ || !model_->FindSpace(first_space_id_)) {
      Stop(kCannotFinish);
      return;
    }
    ImportRows rows = PlanPinnedRows(bookmarks_, first_space_id_, import_map,
                                     model_->tree(), base::BindRepeating(&NewId),
                                     service_->NowMs());
    model_->AddImportedRows(std::move(rows.rows));
    model_->PutImportMap(kSidebarSource, std::move(rows.mapped));
    result_.bookmarks += rows.tabs;
    result_.skipped.duplicates += rows.duplicates;
    result_.skipped.over_cap += rows.over_cap;
    bookmarks_.clear();
    NextProfileAfterExtras();
  }

  void NextProfileAfterExtras() {
    if (!options_.passwords && !options_.cookies) {
      NextProfileAfterSecrets();
      return;
    }
    if (aes_key_) {
      ReadSecrets();
      return;
    }
    if (result_.secrets_denied) {
      NextProfileAfterSecrets();
      return;
    }
    // The keychain is asked once per import; macOS shows its own Allow prompt, which blocks this read.
    service_->SetProgress(ImportProgress{.source = "arc",
                                         .phase = "keychain",
                                         .done = done_count_,
                                         .total = total_,
                                         .skipped = result_.skipped.total()});
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, kBlocking, service_->SecretSource(kSidebarSafeStorageService, kSidebarSafeStorageAccount),
        base::BindOnce(&SidebarJob::OnSecret, weak_factory_.GetWeakPtr()));
  }

  void OnSecret(std::optional<std::string> secret) {
    aes_key_ = secret ? DeriveSafeStorageKey(*secret) : std::nullopt;
    if (secret) {
      OPENSSL_cleanse(secret->data(), secret->size());
    }
    if (!aes_key_) {
      result_.secrets_denied = true;
      NextProfileAfterSecrets();
      return;
    }
    ReadSecrets();
  }

  void ReadSecrets() {
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    service_->SetProgress(ImportProgress{.source = "arc",
                                         .phase = "secrets",
                                         .done = done_count_,
                                         .total = total_,
                                         .skipped = result_.skipped.total()});
    HistoryRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadSidebarSecrets, service_->Root(), p.key, *aes_key_,
                       options_, service_->NowMs(), TempRoot()),
        base::BindOnce(&SidebarJob::OnSecrets, weak_factory_.GetWeakPtr()));
  }

  void OnSecrets(ProfileSecrets read) {
    if (!profile_) {
      Stop(kCannotFinish);
      return;
    }
    result_.secrets_skipped.undecryptable += read.skips.undecryptable;
    result_.secrets_skipped.bad_rows += read.skips.bad_rows;
    result_.secrets_skipped.over_cap += read.skips.over_cap;
    result_.passwords += AddLogins(profile_.get(), read.logins);
    SetCookies(profile_.get(), read.cookies,
               base::BindOnce(&SidebarJob::OnCookiesSet, weak_factory_.GetWeakPtr()));
  }

  void OnCookiesSet(int set, int bad) {
    result_.cookies += set;
    result_.secrets_skipped.bad_rows += bad;
    NextProfileAfterSecrets();
  }

  // Site storage and permissions come with the sign-ins (the cookies checkbox), addresses with their own.
  void NextProfileAfterSecrets() {
    if (!profile_) {
      NextProfileAfterSite();
      return;
    }
    const SidebarProfilePlan& p = plan_.profiles[profile_index_];
    service_->SetProgress(ImportProgress{.source = "arc",
                                         .phase = "site data",
                                         .done = done_count_,
                                         .total = total_,
                                         .skipped = result_.skipped.total()});
    const SiteDataOptions options{
        .storage = options_.cookies, .permissions = options_.cookies, .addresses = options_.autofill};
    HistoryRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadSidebarSiteData, service_->Root(), p.key, profile_->GetPath(),
                       options, TempRoot()),
        base::BindOnce(&SidebarJob::OnSiteData, weak_factory_.GetWeakPtr()));
  }

  void OnSiteData(SiteDataRead read) {
    if (profile_) {
      int skipped = read.skipped;
      result_.site_storage += read.storage.sites();
      result_.permissions += SetPermissions(profile_.get(), read.permissions, &skipped);
      result_.addresses += AddAddresses(profile_.get(), read.addresses);
      result_.secrets_skipped.bad_rows += skipped;
    }
    NextProfileAfterSite();
  }

  void NextProfileAfterSite() {
    ++profile_index_;
    NextProfile();
  }

  void Progress() {
    service_->SetProgress(ImportProgress{.source = "arc",
                                         .phase = "writing",
                                         .done = done_count_,
                                         .total = total_,
                                         .skipped = result_.skipped.total()});
  }

  void Stop(const std::string& error) {
    LOG(WARNING) << "clu: sidebar import stopped: " << error;
    result_.error = error;
    Answer(result_);
  }

  void Answer(base::expected<SidebarImportResult, ImportStartError> result) {
    SidebarCallback done = std::move(done_);
    CluImportService* service = service_;
    if (result.has_value()) {
      service->imported_extensions_ = result.value().extensions;
    }
    service->Finish();
    std::move(done).Run(std::move(result));
  }

  const raw_ptr<CluImportService> service_;
  const SidebarImportOptions options_;
  SidebarCallback done_;
  // Set once the keychain answered with a usable secret.
  std::optional<SafeStorageKey> aes_key_;
  SidebarPlan plan_;
  SidebarImportResult result_;
  size_t profile_index_ = 0;
  size_t space_index_ = 0;
  std::string profile_id_;
  int total_ = 0;
  int done_count_ = 0;
  std::vector<ImportedVisit> visits_;
  // The current source profile's first space, where its bookmarks land, and its bookmarks while they are written.
  std::string first_space_id_;
  std::vector<ImportNode> bookmarks_;
  // The current space's {source key, row id}, for its splits.
  std::map<std::string, std::string> space_rows_;
  std::optional<SidebarSearchEngine> search_engine_;
  base::CallbackListSubscription search_loaded_;
  base::WeakPtr<Profile> profile_;
  base::WeakPtr<CluModelService> model_;
  base::WeakPtrFactory<SidebarJob> weak_factory_{this};
};

// CluImportService -------------------------------------------------------------

// static
CluImportService* CluImportService::Get() {
  if (!g_browser_process || !CluSpacesService::Get()) {
    return nullptr;
  }
  static base::NoDestructor<CluImportService> instance;
  return instance.get();
}

CluImportService::CluImportService()
    : clock_(base::DefaultClock::GetInstance()) {
  // What an import a quit cut short left behind.
  if (const base::FilePath root = TempRoot(); !root.empty()) {
    HistoryRunner()->PostTask(
        FROM_HERE, base::BindOnce(base::IgnoreResult(&base::DeletePathRecursively),
                                  root));
  }
}

CluImportService::~CluImportService() = default;

void CluImportService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CluImportService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

base::FilePath CluImportService::Root() const {
  if (root_for_testing_) {
    return *root_for_testing_;
  }
  // Hidden checks against the fixture folder (an absolute path).
  const base::CommandLine& command_line = *base::CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(kImportRootSwitch)) {
    const base::FilePath root = command_line.GetSwitchValuePath(kImportRootSwitch);
    return root.IsAbsolute() && !root.ReferencesParent() ? root : base::FilePath();
  }
  // Every Chromium test binary runs with --test-type: no root, so a test that forgot to set one reads nothing real.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kTestType)) {
    return base::FilePath();
  }
  base::FilePath home;
  return base::PathService::Get(base::DIR_HOME, &home) ? home
                                                       : base::FilePath();
}

int64_t CluImportService::NowMs() const {
  return clock_->Now().InMillisecondsSinceUnixEpoch();
}

void CluImportService::Detect(base::OnceCallback<void(ImportDetection)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kBlocking, base::BindOnce(&DetectAll, Root()),
      base::BindOnce(
          [](base::WeakPtr<CluImportService> service,
             base::OnceCallback<void(ImportDetection)> done,
             ImportDetection detection) {
            if (service) {
              detection.previous =
                  static_cast<int>(service->PreviousSidebarProfiles().size());
            }
            std::move(done).Run(std::move(detection));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(done)));
}

std::vector<std::string> CluImportService::PreviousSidebarProfiles() const {
  std::vector<std::string> ids;
  CluSpacesService* spaces = CluSpacesService::Get();
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!spaces) {
    return ids;
  }
  std::set<std::string> mapped;
  if (local_state) {
    const std::string prefix = MapKey("profile:");
    for (const auto [key, value] : local_state->GetDict(kCluImportMapPref)) {
      if (key.starts_with(prefix) && value.is_string()) {
        mapped.insert(value.GetString());
      }
    }
  }
  for (const ProfileEntry& profile : spaces->list().profiles()) {
    if (!profile.is_default &&
        (mapped.contains(profile.id) || IsBrokenImportName(profile.name))) {
      ids.push_back(profile.id);
    }
  }
  return ids;
}

void CluImportService::ForgetSidebarImport() {
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return;
  }
  const std::string prefix = base::StrCat({kSidebarSource, "|"});
  ScopedDictPrefUpdate update(local_state, kCluImportMapPref);
  std::vector<std::string> keys;
  for (const auto [key, value] : *update) {
    if (key.starts_with(prefix)) {
      keys.push_back(key);
    }
  }
  for (const std::string& key : keys) {
    update->Remove(key);
  }
}

void CluImportService::RunChrome(ChromeImportRequest request,
                                 ChromeCallback done) {
  if (running_) {
    std::move(done).Run(base::unexpected(ImportStartError::kBusy));
    return;
  }
  const SpaceEntry* space =
      CluSpacesService::Get()->list().FindSpace(request.target_space_id);
  if (!space || !IsChromeProfileDir(request.profile_dir)) {
    std::move(done).Run(base::unexpected(ImportStartError::kNotFound));
    return;
  }
  running_ = true;
  chrome_job_ = std::make_unique<ChromeJob>(this, std::move(request),
                                            space->profile_id, std::move(done));
  chrome_job_->Start();
}

void CluImportService::RunSidebar(SidebarImportOptions options, SidebarCallback done) {
  if (running_) {
    std::move(done).Run(base::unexpected(ImportStartError::kBusy));
    return;
  }
  running_ = true;
  sidebar_job_ = std::make_unique<SidebarJob>(this, options, std::move(done));
  sidebar_job_->Start();
}

void CluImportService::SetProgress(std::optional<ImportProgress> progress) {
  progress_ = std::move(progress);
  for (Observer& observer : observers_) {
    observer.OnImportProgress();
  }
}

void CluImportService::Finish() {
  running_ = false;
  // The job calls this from its own method and returns right after; it goes once the stack unwinds.
  if (chrome_job_) {
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(chrome_job_));
  }
  if (sidebar_job_) {
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(sidebar_job_));
  }
  SetProgress(std::nullopt);
}

void CluImportService::SetRootForTesting(std::optional<base::FilePath> root) {
  root_for_testing_ = std::move(root);
}

base::OnceCallback<std::optional<std::string>()> CluImportService::SecretSource(const char* service,
                                                                               const char* account) const {
  if (secret_for_testing_) {
    return base::BindOnce([](std::optional<std::string> s) { return s; }, *secret_for_testing_);
  }
  // Test binaries and hidden checks against the fixture never ask the real keychain.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kTestType) ||
      base::CommandLine::ForCurrentProcess()->HasSwitch(kImportRootSwitch)) {
    return base::BindOnce([]() -> std::optional<std::string> { return std::nullopt; });
  }
  return base::BindOnce(&ReadSafeStorageSecret, service, account);
}

void CluImportService::SetSafeStorageSecretForTesting(std::optional<std::string> secret) {
  secret_for_testing_ = std::move(secret);
}

const SidebarExtension* CluImportService::FindImportedExtension(std::string_view id) const {
  auto it = std::find_if(imported_extensions_.begin(), imported_extensions_.end(),
                         [&](const SidebarExtension& e) { return e.id == id; });
  return it == imported_extensions_.end() ? nullptr : &*it;
}

void CluImportService::SetImportedExtensionsForTesting(std::vector<SidebarExtension> extensions) {
  imported_extensions_ = std::move(extensions);
}

// static
void CluImportService::SetCookieManagerForTesting(network::mojom::CookieManager* manager) {
  g_cookie_manager_for_testing = manager;
}

void CluImportService::SetClockForTesting(const base::Clock* clock) {
  clock_ = clock ? clock : base::DefaultClock::GetInstance();
}

}  // namespace clu
