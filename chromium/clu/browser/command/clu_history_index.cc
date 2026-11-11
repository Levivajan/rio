// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/command/clu_history_index.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "url/gurl.h"

namespace clu {

namespace {

const void* const kUserDataKey = &kUserDataKey;

}  // namespace

// static
CluHistoryIndex* CluHistoryIndex::GetForProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }
  if (auto* index =
          static_cast<CluHistoryIndex*>(profile->GetUserData(kUserDataKey))) {
    return index;
  }
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::IMPLICIT_ACCESS);
  if (!history) {
    return nullptr;
  }
  auto index = std::make_unique<CluHistoryIndex>(history);
  CluHistoryIndex* raw = index.get();
  profile->SetUserData(kUserDataKey, std::move(index));
  return raw;
}

CluHistoryIndex::CluHistoryIndex(history::HistoryService* history)
    : history_(history) {
  observation_.Observe(history);
}

CluHistoryIndex::~CluHistoryIndex() {
  // A pending mojo callback must still run.
  AnswerPending();
}

void CluHistoryIndex::Sync(int64_t since_rev, SyncCallback callback) {
  // A read in flight may hold a change the last read lacks: the answer waits for it.
  if (!history_ || (!dirty_ && !reading_ && sync_.loaded())) {
    std::move(callback).Run(sync_.Sync(since_rev));
    return;
  }
  pending_.emplace_back(since_rev, std::move(callback));
  Read();
}

void CluHistoryIndex::Delete(const GURL& url) {
  if (history_ && url.is_valid()) {
    history_->DeleteURLs({url});
  }
}

void CluHistoryIndex::Read() {
  if (reading_ || !history_) {
    return;
  }
  reading_ = true;
  dirty_ = false;
  history::QueryOptions options;
  options.max_count = static_cast<int>(kIndexLimit);
  options.duplicate_policy = history::QueryOptions::REMOVE_ALL_DUPLICATES;
  history_->QueryHistory(std::u16string(), options,
                         base::BindOnce(&CluHistoryIndex::OnRead,
                                        weak_ptr_factory_.GetWeakPtr()),
                         &tracker_);
}

void CluHistoryIndex::OnRead(history::QueryResults results) {
  reading_ = false;
  std::vector<IndexRow> rows;
  rows.reserve(results.size());
  size_t skipped = 0;
  for (const history::URLResult& result : results) {
    std::optional<IndexRow> row =
        CleanIndexRow(result.url(), result.title(), result.visit_count(),
                      result.typed_count(), result.last_visit());
    if (row) {
      rows.push_back(std::move(*row));
    } else {
      ++skipped;
    }
  }
  if (skipped) {
    LOG(WARNING) << "clu: the command bar skipped " << skipped
                 << " history rows it cannot open";
  }
  sync_.Refresh(std::move(rows));
  // History changed while it was read: read once more before answering, so the answer has the change. A second
  // change during that read waits for the next sync.
  if (dirty_ && history_ && !reread_) {
    reread_ = true;
    Read();
    return;
  }
  reread_ = false;
  AnswerPending();
}

void CluHistoryIndex::AnswerPending() {
  std::vector<std::pair<int64_t, SyncCallback>> pending;
  pending.swap(pending_);
  for (auto& [since_rev, callback] : pending) {
    std::move(callback).Run(sync_.Sync(since_rev));
  }
}

void CluHistoryIndex::OnURLVisited(
    history::HistoryService* history_service,
    const history::VisitedURLInfo& visited_url_info) {
  dirty_ = true;
}

void CluHistoryIndex::OnURLsModified(history::HistoryService* history_service,
                                     const history::URLRows& changed_urls) {
  dirty_ = true;
}

void CluHistoryIndex::OnHistoryDeletions(
    history::HistoryService* history_service,
    const history::DeletionInfo& deletion_info) {
  dirty_ = true;
}

void CluHistoryIndex::HistoryServiceBeingDeleted(
    history::HistoryService* history_service) {
  observation_.Reset();
  tracker_.TryCancelAll();
  history_ = nullptr;
  reading_ = false;
  reread_ = false;
  AnswerPending();
}

}  // namespace clu
