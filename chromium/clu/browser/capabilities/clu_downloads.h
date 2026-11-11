// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_DOWNLOADS_H_
#define CLU_BROWSER_CAPABILITIES_CLU_DOWNLOADS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "clu/browser/capabilities/clu_download_rows.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class DownloadManager;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

// One window's view of its profile's downloads (docs/chromium/m9.md): Chromium's DownloadManager does the work, this
// turns its items into state rows for the sidebar's downloads button and runs the row actions of download.act by id.
// Opening a file, revealing it and the folder go through Chromium's own shell helpers.
namespace clu {

enum class DownloadAction { kPause, kResume, kCancel, kRetry, kOpen, kReveal, kRemove };

class CluDownloads : public download::AllDownloadItemNotifier::Observer {
 public:
  // `on_change` runs after a download is added or removed, and at most every kDownloadUpdateInterval while one
  // progresses.
  CluDownloads(Profile* profile, base::RepeatingClosure on_change);
  CluDownloads(const CluDownloads&) = delete;
  CluDownloads& operator=(const CluDownloads&) = delete;
  ~CluDownloads() override;

  // The profile's download items, as plain facts.
  std::vector<DownloadFacts> Facts() const;

  // download.act. False when no shown row has `guid` or the row cannot do `action` now (pausing a finished file,
  // opening one that is gone); nothing changes then.
  bool Act(const std::string& guid, DownloadAction action);

  // download.act's drag: the file of the shown, finished row `guid` that is still on disk, as
  // DownloadsDOMHandler::Drag requires; empty otherwise.
  base::FilePath CompletedPath(const std::string& guid) const;

  // download.clear: every finished row leaves the list; the files stay where they are.
  void ClearFinished();

  // download.openFolder: the profile's download folder in Finder.
  void OpenFolder();

  // AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnManagerGoingDown(content::DownloadManager* manager) override;

 private:
  // Tidy Downloads (docs/chromium/parity-command.md): a download this run starts remembers its page; once it
  // finishes with a junk name ("387494876.pdf") it is renamed after that page (ai.tidyDownloads). The on-device model
  // names it when the Mac has one (rio-ai download); the rules name it otherwise.
  static void RememberTidySource(download::DownloadItem* item);
  void TidyWhenDone(download::DownloadItem* item);
  // The model's answer for the download `guid`: its name, else the rules' `fallback`.
  void RenameTidy(const std::string& guid,
                  const std::string& file_name,
                  const std::string& fallback,
                  const GURL& url,
                  std::optional<std::string> answer);

  // A shown item with `guid`, or null.
  download::DownloadItem* Find(const std::string& guid) const;
  void Changed();

  raw_ptr<Profile> profile_;
  base::RepeatingClosure on_change_;
  std::unique_ptr<download::AllDownloadItemNotifier> notifier_;
  base::OneShotTimer throttle_;
  base::WeakPtrFactory<CluDownloads> weak_ptr_factory_{this};
};

// Facts about one Chromium download item (exposed for tests).
DownloadFacts FactsOf(const download::DownloadItem& item);

// Cmd+Shift+J, the sidebar's downloads button and download.openPopover: Chromium's downloads bubble, anchored to the
// window's CluAnchorView, when the window has downloads to list; chrome://downloads otherwise (Chromium's own
// toolbar button does the same).
void ShowDownloadsUi(BrowserWindowInterface* browser);

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_DOWNLOADS_H_
