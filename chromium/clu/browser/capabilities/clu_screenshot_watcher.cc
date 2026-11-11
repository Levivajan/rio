// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_screenshot_watcher.h"

#include <utility>

#include "base/command_line.h"
#include "base/files/file_path_watcher.h"
#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "content/public/common/content_switches.h"

namespace clu {

std::vector<ScreenshotArrival> NewScreenshotArrivals(
    const std::vector<LibraryFile>& files,
    std::set<std::string>& seen,
    base::Time now) {
  std::vector<ScreenshotArrival> arrivals;
  for (const LibraryFile& file : files) {
    if (!seen.insert(file.name).second) {
      continue;
    }
    if (file.kind != "file" &&
        file.modified >= now - kScreenshotArrivalWindow) {
      arrivals.push_back({file.name, file.kind, file.modified});
    }
  }
  return arrivals;
}

// Lives on a blocking pool sequence: watches the folder and lists it after every change.
class CluScreenshotWatcher::Core {
 public:
  using OnListed = base::RepeatingCallback<void(std::vector<LibraryFile>)>;

  Core(base::FilePath folder, OnListed on_listed)
      : folder_(folder.empty() ? LibraryFolder(LibrarySource::kMedia)
                               : std::move(folder)),
        on_listed_(std::move(on_listed)) {
    if (folder_.empty()) {
      return;
    }
    List();
    // Unretained: the watcher is a member, so it never calls back after this is gone.
    watcher_.Watch(folder_, base::FilePathWatcher::Type::kNonRecursive,
                   base::BindRepeating(&Core::OnChange, base::Unretained(this)));
  }
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

 private:
  void OnChange(const base::FilePath& path, bool error) {
    if (!error) {
      List();
    }
  }
  void List() {
    on_listed_.Run(ListLibraryFolder(folder_, LibrarySource::kMedia));
  }

  const base::FilePath folder_;
  OnListed on_listed_;
  base::FilePathWatcher watcher_;
};

CluScreenshotWatcher::CluScreenshotWatcher(base::RepeatingClosure on_change)
    : CluScreenshotWatcher(base::FilePath(), std::move(on_change)) {}

CluScreenshotWatcher::CluScreenshotWatcher(const base::FilePath& folder,
                                           base::RepeatingClosure on_change)
    : on_change_(std::move(on_change)) {
  if (folder.empty() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kTestType)) {
    return;
  }
  // The first listing may ask for macOS's Files and Folders access to the folder, as the Library's Media does.
  core_ = base::SequenceBound<Core>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}),
      folder,
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &CluScreenshotWatcher::OnListed, weak_factory_.GetWeakPtr())));
}

CluScreenshotWatcher::~CluScreenshotWatcher() = default;

void CluScreenshotWatcher::OnListed(std::vector<LibraryFile> files) {
  if (!primed_) {
    // What is already in the folder when Rio starts is not news.
    primed_ = true;
    for (const LibraryFile& file : files) {
      seen_.insert(file.name);
    }
    return;
  }
  std::vector<ScreenshotArrival> fresh =
      NewScreenshotArrivals(files, seen_, base::Time::Now());
  if (fresh.empty()) {
    return;
  }
  fresh.insert(fresh.end(), arrivals_.begin(), arrivals_.end());
  if (fresh.size() > kMaxScreenshotArrivals) {
    fresh.resize(kMaxScreenshotArrivals);
  }
  arrivals_ = std::move(fresh);
  on_change_.Run();
}

}  // namespace clu
