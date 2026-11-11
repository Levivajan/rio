// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_live_folders.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "clu/browser/model/clu_live_feed.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_model_service_factory.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/model/clu_tab_tree.h"
#include "clu/browser/settings/clu_settings_prefs.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

namespace clu {

namespace {

constexpr base::TimeDelta kRefreshEvery = base::Minutes(30);
constexpr base::TimeDelta kFetchTimeout = base::Seconds(30);
// A feed larger than this is no feed Rio reads.
constexpr size_t kMaxFeedBytes = 2 * 1024 * 1024;
constexpr char kKindKey[] = "kind";
constexpr char kUrlKey[] = "url";
constexpr char kProfileKey[] = "profile";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rio_live_folder", R"(
        semantics {
          sender: "Rio Live Folders"
          description:
            "Reads a public RSS or Atom feed (or a public GitHub repository's "
            "releases or commits feed) that the user chose for a Live Folder "
            "in the sidebar, to list its newest entries."
          trigger:
            "The user makes a Live Folder from the space menu; then every 30 "
            "minutes while Rio runs, for every Live Folder."
          data: "None. No cookies or credentials are sent."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Delete the Live Folder to stop it."
          policy_exception_justification: "Not implemented."
        })");

PrefService* LocalState() {
  return g_browser_process ? g_browser_process->local_state() : nullptr;
}

Profile* ProfileAt(const base::FilePath& path) {
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  return manager ? manager->GetProfileByPath(path) : nullptr;
}

// The profile's model once it has loaded, else null.
CluModelService* LoadedModel(Profile* profile) {
  CluModelService* m = profile ? CluModelServiceFactory::GetForProfile(profile)
                               : nullptr;
  return m && m->loaded() ? m : nullptr;
}

}  // namespace

CluLiveFolders& CluLiveFolders::Get() {
  static base::NoDestructor<CluLiveFolders> folders;
  return *folders;
}

CluLiveFolders::CluLiveFolders() = default;
CluLiveFolders::~CluLiveFolders() = default;

void CluLiveFolders::EnsureStarted() {
  if (started_) {
    return;
  }
  started_ = true;
  timer_.Start(FROM_HERE, kRefreshEvery,
               base::BindRepeating(&CluLiveFolders::RefreshAll,
                                   base::Unretained(this)));
  RefreshAll();
}

base::expected<std::string, std::string> CluLiveFolders::Create(
    CluWindowState& window,
    std::string_view space_id,
    std::string_view kind,
    std::string_view source) {
  CluModelService* m = window.model();
  PrefService* local_state = LocalState();
  if (!m || !local_state) {
    return base::unexpected("no-rows");
  }
  const std::optional<GURL> url = LiveFolderFeedUrl(kind, source);
  if (!url) {
    return base::unexpected("bad-source");
  }
  base::expected<std::string, TreeError> folder = m->mutable_tree().CreateFolder(
      space_id, std::string(), LiveFolderName(kind, *url), std::nullopt,
      m->NowMs());
  if (!folder.has_value()) {
    return base::unexpected("invalid");
  }
  m->mutable_tree().MutableSpaceState(space_id).pinned_collapsed = false;
  m->Commit();
  {
    ScopedDictPrefUpdate update(local_state, kCluLiveFoldersPref);
    update->Set(folder.value(),
                base::DictValue()
                    .Set(kKindKey, kind)
                    .Set(kUrlKey, url->spec())
                    .Set(kProfileKey,
                         window.profile()->GetPath().BaseName().AsUTF8Unsafe()));
  }
  Fetch(window.profile()->GetPath(), folder.value(), *url,
        /*take_title=*/kind == kLiveFeed, window.GetWeakPtr());
  return base::ok(folder.value());
}

void CluLiveFolders::RefreshAll() {
  PrefService* local_state = LocalState();
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!local_state || !manager) {
    return;
  }
  std::vector<std::string> gone;
  for (const auto [folder_id, entry] : local_state->GetDict(kCluLiveFoldersPref)) {
    const base::DictValue* dict = entry.GetIfDict();
    const std::string* url = dict ? dict->FindString(kUrlKey) : nullptr;
    const std::string* profile_name = dict ? dict->FindString(kProfileKey) : nullptr;
    if (!url || !profile_name || !GURL(*url).SchemeIsHTTPOrHTTPS()) {
      gone.push_back(folder_id);
      continue;
    }
    const base::FilePath path =
        manager->user_data_dir().AppendASCII(*profile_name);
    CluModelService* m = LoadedModel(ProfileAt(path));
    if (!m) {
      // The profile is not loaded now; its folders wait for the next refresh.
      continue;
    }
    const TabNode* node = m->tree().Get(folder_id);
    if (!node || !node->is_folder()) {
      gone.push_back(folder_id);
      continue;
    }
    Fetch(path, folder_id, GURL(*url), /*take_title=*/false, nullptr);
  }
  if (!gone.empty()) {
    ScopedDictPrefUpdate update(local_state, kCluLiveFoldersPref);
    for (const std::string& id : gone) {
      update->Remove(id);
    }
  }
}

void CluLiveFolders::Fetch(const base::FilePath& profile_path,
                           const std::string& folder_id,
                           const GURL& url,
                           bool take_title,
                           base::WeakPtr<CluWindowState> window) {
  Profile* profile = ProfileAt(profile_path);
  if (!profile) {
    return;
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  // Public data only: no cookies, no HTTP auth, nothing signed in.
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(kFetchTimeout);
  network::SimpleURLLoader* raw = loader.get();
  loaders_.insert(std::move(loader));
  raw->DownloadToString(
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&CluLiveFolders::OnFetched,
                     weak_ptr_factory_.GetWeakPtr(), raw, profile_path,
                     folder_id, url, take_title, std::move(window)),
      kMaxFeedBytes);
}

void CluLiveFolders::OnFetched(network::SimpleURLLoader* loader,
                               base::FilePath profile_path,
                               std::string folder_id,
                               GURL url,
                               bool take_title,
                               base::WeakPtr<CluWindowState> window,
                               std::optional<std::string> body) {
  loaders_.erase(loaders_.find(loader));
  if (!body || body->empty()) {
    Failed(window);
    return;
  }
  data_decoder::DataDecoder::ParseXmlIsolated(
      *body, data_decoder::mojom::XmlParser::WhitespaceBehavior::kIgnore,
      base::BindOnce(&CluLiveFolders::OnParsed, weak_ptr_factory_.GetWeakPtr(),
                     std::move(profile_path), std::move(folder_id),
                     std::move(url), take_title, std::move(window)));
}

void CluLiveFolders::OnParsed(base::FilePath profile_path,
                              std::string folder_id,
                              GURL url,
                              bool take_title,
                              base::WeakPtr<CluWindowState> window,
                              base::expected<base::Value, std::string> parsed) {
  const std::optional<LiveFeed> feed =
      parsed.has_value() ? ParseLiveFeed(*parsed, url) : std::nullopt;
  if (!feed) {
    Failed(window);
    return;
  }
  CluModelService* m = LoadedModel(ProfileAt(profile_path));
  const TabNode* folder = m ? m->tree().Get(folder_id) : nullptr;
  if (!folder || !folder->is_folder()) {
    return;
  }
  const std::string space_id = folder->space_id;
  CluTabTree& tree = m->mutable_tree();
  std::vector<LiveRow> rows;
  for (const std::string& id : tree.Children(folder_id)) {
    const TabNode* row = tree.Get(id);
    if (row && row->is_tab()) {
      rows.push_back({id, row->pinned_url.value_or(row->url),
                      m->FindContents(id) != nullptr});
    }
  }
  const LiveFolderPlan plan = PlanLiveFolder(rows, feed->items);
  for (const std::string& id : plan.remove) {
    tree.RemoveTab(id);
  }
  // New entries go to the folder's top, newest first.
  for (const auto& [index, item] : plan.insert) {
    NewTab init;
    init.url = item.url;
    init.title = item.title;
    if (!tree.InsertTab(init, DropTarget::Pinned(space_id, folder_id, index),
                        m->NowMs())
             .has_value()) {
      break;
    }
  }
  // A feed folder takes the feed's own name ("Hacker News") once, when it is made.
  if (std::string name = take_title ? CleanName(feed->title) : std::string();
      !name.empty()) {
    if (TabNode* node = tree.Mutable(folder_id)) {
      node->title_override = std::move(name);
    }
  }
  m->Commit();
}

void CluLiveFolders::Failed(base::WeakPtr<CluWindowState> window) {
  if (window) {
    window->ShowToast("Couldn't read this feed", std::nullopt,
                      base::Milliseconds(2500));
  }
}

}  // namespace clu
