// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_COMMAND_CLU_HISTORY_INDEX_H_
#define CLU_BROWSER_COMMAND_CLU_HISTORY_INDEX_H_

#include <cstdint>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "clu/browser/command/clu_index_sync.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_service_observer.h"

class GURL;
class Profile;

namespace history {
class QueryResults;
}  // namespace history

namespace clu {

// The command bar's history rows of one profile (command.syncIndex, command.deleteEntry): Chromium's HistoryService
// read through CluIndexSync. It hangs off the Profile and reads history again only after history changed since its
// last read (a visit, a title, a deletion), so most opens cost one cheap delta. Off-the-record profiles have no
// history service here, so a private window's bar lists no history rows.
class CluHistoryIndex : public base::SupportsUserData::Data,
                        public history::HistoryServiceObserver {
 public:
  using SyncCallback = base::OnceCallback<void(IndexDelta)>;

  // The index of `profile`, made on first use. Null when the profile keeps no history.
  static CluHistoryIndex* GetForProfile(Profile* profile);

  explicit CluHistoryIndex(history::HistoryService* history);
  CluHistoryIndex(const CluHistoryIndex&) = delete;
  CluHistoryIndex& operator=(const CluHistoryIndex&) = delete;
  ~CluHistoryIndex() override;

  // What an overlay at `since_rev` is missing. Runs `callback` once history was read when it changed; always runs it,
  // with the last read, if history goes away first.
  void Sync(int64_t since_rev, SyncCallback callback);

  // command.deleteEntry: removes every visit of `url` (the exact stored URL) from history.
  void Delete(const GURL& url);

  // history::HistoryServiceObserver:
  void OnURLVisited(history::HistoryService* history_service,
                    const history::VisitedURLInfo& visited_url_info) override;
  void OnURLsModified(history::HistoryService* history_service,
                      const history::URLRows& changed_urls) override;
  void OnHistoryDeletions(history::HistoryService* history_service,
                          const history::DeletionInfo& deletion_info) override;
  void HistoryServiceBeingDeleted(
      history::HistoryService* history_service) override;

  // Tests: reads history again on the next sync even without a change.
  void MarkDirtyForTesting() { dirty_ = true; }

 private:
  void Read();
  void OnRead(history::QueryResults results);
  void AnswerPending();

  raw_ptr<history::HistoryService> history_;
  CluIndexSync sync_;
  // History changed since the last read (true until the first one).
  bool dirty_ = true;
  bool reading_ = false;
  bool reread_ = false;
  std::vector<std::pair<int64_t, SyncCallback>> pending_;
  base::CancelableTaskTracker tracker_;
  base::ScopedObservation<history::HistoryService,
                          history::HistoryServiceObserver>
      observation_{this};
  base::WeakPtrFactory<CluHistoryIndex> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_COMMAND_CLU_HISTORY_INDEX_H_
