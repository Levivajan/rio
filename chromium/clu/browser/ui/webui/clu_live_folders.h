// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_LIVE_FOLDERS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_LIVE_FOLDERS_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "url/gurl.h"

class Profile;

namespace network {
class SimpleURLLoader;
}  // namespace network

// Live Folders (clu_live_feed.h): the space menu's Live Folders ▸ makes a pinned folder that follows a public
// feed. One instance for the browser process: it fetches each live folder's feed with no cookies or credentials
// (nothing signed in is ever read), parses it in the data decoder's sandboxed process, and puts the newest
// kMaxLiveFolderItems entries in the folder as pinned rows without pages, new ones on top. It refreshes every live
// folder of the loaded profiles when the first window opens and every 30 minutes after. Which folders are live is kept
// in Local State (`clu.live_folders`: {folder id: {kind, url, profile}}); a deleted folder's entry goes at the next
// refresh of its profile.
namespace clu {

class CluWindowState;

class CluLiveFolders {
 public:
  static CluLiveFolders& Get();

  CluLiveFolders(const CluLiveFolders&) = delete;
  CluLiveFolders& operator=(const CluLiveFolders&) = delete;

  // The first window starts the refresh timer and refreshes every live folder once.
  void EnsureStarted();

  // folders.createLive: a folder at the end of `space_id`'s pinned rows following the feed `source` names. Answers
  // the folder's id, or "bad-source" for a source that names no feed (the prompt stays open). A feed that cannot be
  // read leaves the empty folder and toasts in `window`.
  base::expected<std::string, std::string> Create(
      CluWindowState& window,
      std::string_view space_id,
      std::string_view kind,
      std::string_view source);

  // Every live folder of the loaded profiles now (also the timer's work).
  void RefreshAll();

 private:
  friend class base::NoDestructor<CluLiveFolders>;
  CluLiveFolders();
  ~CluLiveFolders();

  void Fetch(const base::FilePath& profile_path,
             const std::string& folder_id,
             const GURL& url,
             bool take_title,
             base::WeakPtr<CluWindowState> window);
  void OnFetched(network::SimpleURLLoader* loader,
                 base::FilePath profile_path,
                 std::string folder_id,
                 GURL url,
                 bool take_title,
                 base::WeakPtr<CluWindowState> window,
                 std::optional<std::string> body);
  void OnParsed(base::FilePath profile_path,
                std::string folder_id,
                GURL url,
                bool take_title,
                base::WeakPtr<CluWindowState> window,
                base::expected<base::Value, std::string> parsed);
  void Failed(base::WeakPtr<CluWindowState> window);

  bool started_ = false;
  base::RepeatingTimer timer_;
  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator>
      loaders_;
  base::WeakPtrFactory<CluLiveFolders> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_LIVE_FOLDERS_H_
