// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_downloads.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.h"
#include "clu/browser/ai/clu_ai_client.h"
#include "clu/browser/model/clu_tidy.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace clu {

namespace {

DownloadPhase PhaseOf(const download::DownloadItem& item) {
  switch (item.GetState()) {
    case download::DownloadItem::IN_PROGRESS:
      return item.IsPaused() ? DownloadPhase::kPaused
                             : DownloadPhase::kProgressing;
    case download::DownloadItem::COMPLETE:
      return DownloadPhase::kCompleted;
    case download::DownloadItem::CANCELLED:
      return DownloadPhase::kCancelled;
    case download::DownloadItem::INTERRUPTED:
    case download::DownloadItem::MAX_DOWNLOAD_STATE:
      return DownloadPhase::kInterrupted;
  }
  return DownloadPhase::kInterrupted;
}

bool IsShown(const download::DownloadItem& item) {
  return !item.IsTransient() && !item.IsTemporary();
}

}  // namespace

DownloadFacts FactsOf(const download::DownloadItem& item) {
  DownloadFacts facts;
  facts.guid = item.GetGuid();
  facts.file_name = item.GetFileNameToReportUser().AsUTF8Unsafe();
  facts.url = StateUrl(item.GetURL());
  facts.phase = PhaseOf(item);
  facts.received_bytes = item.GetReceivedBytes();
  facts.total_bytes = item.GetTotalBytes();
  facts.bytes_per_second = item.CurrentSpeed();
  base::TimeDelta remaining;
  if (item.TimeRemaining(&remaining)) {
    facts.remaining = remaining;
  }
  facts.start_time = item.GetStartTime();
  facts.end_time = item.GetEndTime();
  facts.externally_removed = item.GetFileExternallyRemoved();
  facts.hidden = !IsShown(item);
  return facts;
}

CluDownloads::CluDownloads(Profile* profile, base::RepeatingClosure on_change)
    : profile_(profile), on_change_(std::move(on_change)) {
  if (content::DownloadManager* manager = profile_->GetDownloadManager()) {
    notifier_ =
        std::make_unique<download::AllDownloadItemNotifier>(manager, this);
  }
}

CluDownloads::~CluDownloads() = default;

std::vector<DownloadFacts> CluDownloads::Facts() const {
  std::vector<DownloadFacts> facts;
  content::DownloadManager* manager =
      notifier_ ? notifier_->GetManager() : nullptr;
  if (!manager) {
    return facts;
  }
  download::SimpleDownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  facts.reserve(items.size());
  for (download::DownloadItem* item : items) {
    if (item) {
      facts.push_back(FactsOf(*item));
    }
  }
  return facts;
}

download::DownloadItem* CluDownloads::Find(const std::string& guid) const {
  content::DownloadManager* manager =
      notifier_ ? notifier_->GetManager() : nullptr;
  download::DownloadItem* item =
      manager ? manager->GetDownloadByGuid(guid) : nullptr;
  return item && IsShown(*item) ? item : nullptr;
}

base::FilePath CluDownloads::CompletedPath(const std::string& guid) const {
  download::DownloadItem* item = Find(guid);
  if (!item || item->GetState() != download::DownloadItem::COMPLETE ||
      item->GetFileExternallyRemoved()) {
    return base::FilePath();
  }
  return item->GetTargetFilePath();
}

bool CluDownloads::Act(const std::string& guid, DownloadAction action) {
  download::DownloadItem* item = Find(guid);
  if (!item) {
    return false;
  }
  const download::DownloadItem::DownloadState state = item->GetState();
  switch (action) {
    case DownloadAction::kPause:
      if (state != download::DownloadItem::IN_PROGRESS || item->IsPaused()) {
        return false;
      }
      item->Pause();
      return true;
    case DownloadAction::kResume:
    case DownloadAction::kRetry:
      // Resume continues a paused file; Retry picks up an interrupted one where Chromium can (validators and the
      // partial file), and Chromium restarts it from zero where it cannot. A cancelled file has nothing to resume:
      // Chromium's bubble and chrome://downloads download it again.
      if (!item->CanResume()) {
        return false;
      }
      item->Resume(/*user_resume=*/true);
      return true;
    case DownloadAction::kCancel:
      if (item->IsDone()) {
        return false;
      }
      item->Cancel(/*user_cancel=*/true);
      return true;
    case DownloadAction::kOpen:
      // A removed file shows "Removed" and cannot open. Apps, installers and scripts carry the quarantine
      // attribute Chromium sets, so macOS asks before running them.
      if (state != download::DownloadItem::COMPLETE ||
          item->GetFileExternallyRemoved() || !item->CanOpenDownload()) {
        return false;
      }
      item->OpenDownload();
      return true;
    case DownloadAction::kReveal:
      if (item->GetFileExternallyRemoved() || !item->CanShowInFolder()) {
        return false;
      }
      item->ShowDownloadInShell();
      return true;
    case DownloadAction::kRemove:
      // The row leaves the list; the file stays.
      if (!item->IsDone()) {
        return false;
      }
      item->Remove();
      return true;
  }
  return false;
}

void CluDownloads::ClearFinished() {
  content::DownloadManager* manager =
      notifier_ ? notifier_->GetManager() : nullptr;
  if (!manager) {
    return;
  }
  download::SimpleDownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  // Remove() destroys the item, so the guids are collected first.
  std::vector<std::string> done;
  for (download::DownloadItem* item : items) {
    if (item && IsShown(*item) && item->IsDone()) {
      done.push_back(item->GetGuid());
    }
  }
  for (const std::string& guid : done) {
    if (download::DownloadItem* item = manager->GetDownloadByGuid(guid)) {
      item->Remove();
    }
  }
}

void CluDownloads::OpenFolder() {
  DownloadPrefs* prefs = DownloadPrefs::FromBrowserContext(profile_);
  if (!prefs) {
    return;
  }
  platform_util::OpenItem(profile_, prefs->DownloadPath(),
                          platform_util::OPEN_FOLDER,
                          platform_util::OpenOperationCallback());
}

namespace {

// The page a download started from, kept on the item until it finishes (Tidy Downloads). It also marks a download
// this run started, so a finished one restored from history is never renamed. Every window of the profile watches the
// same items; the first to see the item finish takes the mark, so a file is renamed once.
constexpr char kTidySourceKey[] = "clu.tidy.source";

struct TidySource : public base::SupportsUserData::Data {
  TidySource(std::string title, GURL url)
      : title(std::move(title)), url(std::move(url)) {}
  std::string title;
  GURL url;
};

// The app settings AI tab's Tidy Downloads switch (docs/chromium/parity-settings.md section 1): on unless stored as
// false.
bool TidyDownloadsOn() {
  CluAppSettings* settings = CluAppSettings::Get();
  return !settings ||
         settings->Global().FindBool("ai.tidyDownloads").value_or(true);
}

}  // namespace

// static
void CluDownloads::RememberTidySource(download::DownloadItem* item) {
  if (item->GetState() != download::DownloadItem::IN_PROGRESS ||
      item->IsSavePackageDownload() || item->GetUserData(kTidySourceKey)) {
    return;
  }
  content::WebContents* contents =
      content::DownloadItemUtils::GetWebContents(item);
  std::string title =
      contents ? base::UTF16ToUTF8(contents->GetTitle()) : std::string();
  item->SetUserData(kTidySourceKey, std::make_unique<TidySource>(
                                        std::move(title), item->GetTabUrl()));
}

void CluDownloads::TidyWhenDone(download::DownloadItem* item) {
  if (item->GetState() != download::DownloadItem::COMPLETE) {
    return;
  }
  auto* source = static_cast<TidySource*>(item->GetUserData(kTidySourceKey));
  if (!source) {
    return;
  }
  const std::string title = source->title;
  const GURL url = source->url;
  // Renaming updates the item again; without the mark that update does nothing here.
  item->RemoveUserData(kTidySourceKey);
  if (!TidyDownloadsOn() || item->GetFileExternallyRemoved() ||
      item->GetTargetFilePath().empty()) {
    return;
  }
  const std::string file_name =
      item->GetTargetFilePath().BaseName().AsUTF8Unsafe();
  // The rules decide which names are junk and name them when the model cannot.
  const std::optional<std::string> tidy =
      TidyDownloadName(file_name, title, url);
  if (!tidy) {
    return;
  }
  AskOnDeviceModel({"download", file_name, title, url.spec()},
                   base::BindOnce(&CluDownloads::RenameTidy,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  item->GetGuid(), file_name, *tidy, url));
}

void CluDownloads::RenameTidy(const std::string& guid,
                              const std::string& file_name,
                              const std::string& fallback,
                              const GURL& url,
                              std::optional<std::string> answer) {
  content::DownloadManager* manager =
      notifier_ ? notifier_->GetManager() : nullptr;
  download::DownloadItem* item =
      manager ? manager->GetDownloadByGuid(guid) : nullptr;
  if (!item || item->GetState() != download::DownloadItem::COMPLETE ||
      item->GetFileExternallyRemoved() ||
      item->GetTargetFilePath().BaseName().AsUTF8Unsafe() != file_name) {
    return;
  }
  // The model's words go through the same rules as a page title: cut, refused characters replaced, the extension kept.
  std::optional<std::string> name;
  if (const std::optional<std::string> clean =
          answer ? CleanModelTitle(*answer, kMaxTidyDownloadStem)
                 : std::nullopt) {
    name = TidyDownloadName(file_name, *clean, url);
  }
  // A name already taken in the folder fails the rename, and the file keeps its own (FAILURE_NAME_CONFLICT).
  item->Rename(base::FilePath::FromUTF8Unsafe(name.value_or(fallback)),
               base::DoNothing());
}

void CluDownloads::OnDownloadCreated(content::DownloadManager* manager,
                                     download::DownloadItem* item) {
  RememberTidySource(item);
  throttle_.Stop();
  on_change_.Run();
}

void CluDownloads::OnDownloadUpdated(content::DownloadManager* manager,
                                     download::DownloadItem* item) {
  TidyWhenDone(item);
  // A finished, paused or failed download shows at once; progress at most every kDownloadUpdateInterval.
  if (item->GetState() != download::DownloadItem::IN_PROGRESS ||
      item->IsPaused()) {
    throttle_.Stop();
    on_change_.Run();
    return;
  }
  Changed();
}

void CluDownloads::OnDownloadRemoved(content::DownloadManager* manager,
                                     download::DownloadItem* item) {
  throttle_.Stop();
  // The item is still in the manager's list during this call; state is built after it is gone.
  throttle_.Start(FROM_HERE, base::TimeDelta(), on_change_);
}

void CluDownloads::OnManagerGoingDown(content::DownloadManager* manager) {
  throttle_.Stop();
}

void CluDownloads::Changed() {
  if (!throttle_.IsRunning()) {
    throttle_.Start(FROM_HERE, kDownloadUpdateInterval, on_change_);
  }
}

void ShowDownloadsUi(BrowserWindowInterface* browser) {
  if (!browser) {
    return;
  }
  // InvokeUI() opens the bubble when it has rows and no bubble is open, chrome://downloads otherwise.
  if (DownloadToolbarUIController* controller =
          DownloadToolbarUIController::From(browser)) {
    controller->InvokeUI();
    return;
  }
  chrome::ShowDownloads(browser);
}

}  // namespace clu
