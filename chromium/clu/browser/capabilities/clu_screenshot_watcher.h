// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_CAPABILITIES_CLU_SCREENSHOT_WATCHER_H_
#define CLU_BROWSER_CAPABILITIES_CLU_SCREENSHOT_WATCHER_H_

#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/time/time.h"
#include "clu/browser/capabilities/clu_download_rows.h"
#include "clu/browser/capabilities/clu_library_files.h"

// The moment a screenshot or screen recording lands in the screenshot folder (LibraryFolder(kMedia), the
// com.apple.screencapture location), the sidebar shows a card above the bottom bar. A FilePathWatcher (FSEvents on
// the Mac) watches the folder on a blocking pool sequence; each change lists the folder again, and a file that was
// not there when the watch began and was written in the last kScreenshotArrivalWindow is an arrival. Finished
// downloads need nothing here: the renderer sees them complete in the downloads rows.
namespace clu {

// A file older than this when it is first seen is not news (a file copied or touched into the folder).
inline constexpr base::TimeDelta kScreenshotArrivalWindow = base::Minutes(1);

// The arrivals among `files` (newest first, as ListLibraryFolder returns them) that are not in `seen`, written at or
// after `now - kScreenshotArrivalWindow`. Every name in `files` joins `seen`.
std::vector<ScreenshotArrival> NewScreenshotArrivals(
    const std::vector<LibraryFile>& files,
    std::set<std::string>& seen,
    base::Time now);

class CluScreenshotWatcher {
 public:
  // `on_change` runs on the UI sequence after an arrival joins arrivals(). The first form watches the screenshot
  // folder, or nothing under --test-type (every Chromium test binary), so tests never read the real folder; tests
  // pass a temp `folder`.
  explicit CluScreenshotWatcher(base::RepeatingClosure on_change);
  CluScreenshotWatcher(const base::FilePath& folder,
                       base::RepeatingClosure on_change);
  CluScreenshotWatcher(const CluScreenshotWatcher&) = delete;
  CluScreenshotWatcher& operator=(const CluScreenshotWatcher&) = delete;
  ~CluScreenshotWatcher();

  // Newest first, at most kMaxScreenshotArrivals.
  const std::vector<ScreenshotArrival>& arrivals() const { return arrivals_; }
  // True once the folder's first listing is in: files written from then on are arrivals.
  bool primed() const { return primed_; }

 private:
  class Core;

  // A fresh listing of the folder; the first one only fills `seen_`.
  void OnListed(std::vector<LibraryFile> files);

  base::RepeatingClosure on_change_;
  base::SequenceBound<Core> core_;
  bool primed_ = false;
  std::set<std::string> seen_;
  std::vector<ScreenshotArrival> arrivals_;
  base::WeakPtrFactory<CluScreenshotWatcher> weak_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_CAPABILITIES_CLU_SCREENSHOT_WATCHER_H_
