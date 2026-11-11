// Copyright 2026 The Rio Authors. All rights reserved.

// CluModelService's splits (docs/plan/05-multitask.md "Split view",
// docs/chromium/m6.md): which tab rows form a split, in which order and
// direction, and the first pane's share. Membership changes write with the tree
// in Write(); a resize writes 500 ms after the last change. Windows show a
// split through Chromium's own two-pane split tabs.

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/uuid.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_tab_rules.h"

namespace clu {

const SplitRow* CluModelService::FindSplit(std::string_view split_id) const {
  const auto it = splits_.find(split_id);
  return it == splits_.end() ? nullptr : &it->second;
}

const SplitRow* CluModelService::SplitOf(std::string_view tab_id) const {
  const TabNode* node = tree_.Get(tab_id);
  if (!node || !node->split_id) {
    return nullptr;
  }
  const SplitRow* split = FindSplit(*node->split_id);
  if (!split ||
      std::ranges::find(split->tab_ids, tab_id) == split->tab_ids.end()) {
    return nullptr;
  }
  return split;
}

base::expected<std::string, std::string> CluModelService::CreateSplit(
    const std::vector<std::string>& tab_ids,
    SplitDir dir,
    std::string_view focused_tab_id) {
  if (tab_ids.size() < 2 || !UniqueIds(tab_ids)) {
    return base::unexpected("not-found");
  }
  if (tab_ids.size() > kMaxSplitPanes) {
    return base::unexpected("split-full");
  }
  std::string space_id;
  for (const std::string& id : tab_ids) {
    const TabNode* node = tree_.Get(id);
    if (!node || !node->is_tab()) {
      return base::unexpected("not-found");
    }
    if (node->section == Section::kFavorite) {
      return base::unexpected("favorite");
    }
    if (space_id.empty()) {
      space_id = node->space_id;
    } else if (node->space_id != space_id) {
      return base::unexpected("other-space");
    }
  }
  // Every move is checked before anything changes, so a refused split leaves
  // the old ones as they were.
  {
    const TabNode* first = tree_.Get(tab_ids[0]);
    const TabNode* second = tree_.Get(tab_ids[1]);
    DropTarget check;
    check.section = first->section;
    check.space_id = first->space_id;
    check.parent_id = first->parent_id;
    check.index = first->position;
    if (ContainerKeyOf(*second) != ContainerKeyOf(*first) &&
        !tree_.ValidateMove(tab_ids[1], check).has_value()) {
      return base::unexpected("not-found");
    }
  }
  for (const std::string& id : tab_ids) {
    if (const SplitRow* old = SplitOf(id)) {
      DissolveSplit(old->id);
    }
  }
  // The second pane joins the first's list right after it (F8 move rules: a
  // Today row dropped among pinned rows becomes pinned, as a sidebar drag would
  // make it).
  const TabNode* anchor = tree_.Get(tab_ids[0]);
  DropTarget target;
  target.section = anchor->section;
  target.space_id = anchor->space_id;
  target.parent_id = anchor->parent_id;
  const std::string list = ContainerKeyOf(*anchor);
  for (size_t i = 1; i < tab_ids.size(); ++i) {
    const TabNode* node = tree_.Get(tab_ids[i]);
    const bool same_list = ContainerKeyOf(*node) == list;
    if (same_list && node->position == anchor->position + static_cast<int>(i)) {
      continue;
    }
    // `index` counts the list without the moved row.
    int index = anchor->position + static_cast<int>(i);
    if (same_list && node->position < anchor->position) {
      index -= 1;
    }
    target.index = std::max(0, index);
    if (!tree_.Move(tab_ids[i], target, NowMs()).has_value()) {
      return base::unexpected("not-found");
    }
    anchor = tree_.Get(tab_ids[0]);
  }

  SplitRow split;
  split.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  split.space_id = space_id;
  split.dir = dir;
  split.tab_ids = tab_ids;
  split.focused_tab_id =
      std::ranges::find(tab_ids, focused_tab_id) != tab_ids.end()
          ? std::string(focused_tab_id)
          : tab_ids[0];
  split.created_at = split.updated_at = NowMs();
  for (const std::string& id : tab_ids) {
    tree_.Mutable(id)->split_id = split.id;
  }
  const std::string id = split.id;
  splits_.emplace(id, std::move(split));
  TouchSplit(id);
  Commit();
  return base::ok(id);
}

bool CluModelService::ArrangeSplit(std::string_view split_id,
                                   const std::vector<std::string>& tab_ids,
                                   SplitDir dir) {
  const auto it = splits_.find(split_id);
  if (it == splits_.end() || tab_ids.size() != it->second.tab_ids.size() ||
      !std::ranges::is_permutation(tab_ids, it->second.tab_ids)) {
    return false;
  }
  SplitRow& split = it->second;
  if (split.tab_ids == tab_ids && split.dir == dir) {
    return true;
  }
  // The share stays with the pane that had it.
  if (split.tab_ids.size() == 2 && split.tab_ids[0] != tab_ids[0]) {
    split.ratio = 1.0 - split.ratio;
  }
  split.tab_ids = tab_ids;
  split.dir = dir;
  split.updated_at = NowMs();
  TouchSplit(split.id);
  Commit();
  return true;
}

bool CluModelService::SetSplitRatio(std::string_view split_id, double ratio) {
  const auto it = splits_.find(split_id);
  if (it == splits_.end()) {
    return false;
  }
  const double clamped =
      ClampSplitRatio(ratio, /*avail=*/0, /*min_pane=*/0, it->second.ratio);
  if (clamped == it->second.ratio) {
    return true;
  }
  it->second.ratio = clamped;
  it->second.updated_at = NowMs();
  dirty_splits_.insert(it->second.id);
  split_write_timer_.Start(
      FROM_HERE, kSplitResizeWrite,
      base::BindOnce(&CluModelService::Write, base::Unretained(this)));
  NotifyChanged();
  return true;
}

bool CluModelService::SetSplitSizes(std::string_view split_id,
                                    const std::vector<double>& sizes) {
  const auto it = splits_.find(split_id);
  if (it == splits_.end() || it->second.tab_ids.size() < 3 ||
      PaneShares(sizes, it->second.tab_ids.size()) != sizes) {
    return false;
  }
  if (it->second.sizes == sizes) {
    return true;
  }
  it->second.sizes = sizes;
  it->second.updated_at = NowMs();
  dirty_splits_.insert(it->second.id);
  split_write_timer_.Start(
      FROM_HERE, kSplitResizeWrite,
      base::BindOnce(&CluModelService::Write, base::Unretained(this)));
  NotifyChanged();
  return true;
}

void CluModelService::SetSplitFocus(std::string_view split_id,
                                    std::string_view tab_id) {
  const auto it = splits_.find(split_id);
  if (it == splits_.end() || it->second.focused_tab_id == tab_id ||
      std::ranges::find(it->second.tab_ids, tab_id) ==
          it->second.tab_ids.end()) {
    return;
  }
  it->second.focused_tab_id = std::string(tab_id);
  TouchSplit(it->second.id);
  CommitSoon();
}

void CluModelService::DissolveSplit(std::string_view split_id) {
  const auto it = splits_.find(split_id);
  if (it == splits_.end()) {
    return;
  }
  for (const std::string& id : it->second.tab_ids) {
    TabNode* node = tree_.Mutable(id);
    if (node && node->split_id == it->second.id) {
      node->split_id.reset();
    }
  }
  dirty_splits_.insert(it->second.id);
  splits_.erase(it);
  Commit();
}

void CluModelService::ValidateSplits() {
  std::vector<std::string> broken;
  for (const auto& [id, split] : splits_) {
    int held = 0;
    for (const std::string& tab_id : split.tab_ids) {
      const TabNode* node = tree_.Get(tab_id);
      if (node && node->is_tab() && node->section != Section::kFavorite &&
          node->space_id == split.space_id && node->split_id == id) {
        ++held;
      }
    }
    if (held < 2) {
      broken.push_back(id);
    }
  }
  for (const std::string& id : broken) {
    for (const std::string& tab_id : splits_.at(id).tab_ids) {
      TabNode* node = tree_.Mutable(tab_id);
      if (node && node->split_id == id) {
        node->split_id.reset();
      }
    }
    dirty_splits_.insert(id);
    splits_.erase(id);
  }
}

void CluModelService::TakeSplitWrites(StoreWrite& write) {
  split_write_timer_.Stop();
  for (const std::string& id : dirty_splits_) {
    const auto it = splits_.find(id);
    if (it == splits_.end()) {
      write.split_deletes.push_back(id);
    } else {
      write.split_upserts.push_back(it->second);
    }
  }
  dirty_splits_.clear();
}

void CluModelService::ArchiveDetachedPage(const PageCapture& page,
                                          const std::string& space_id,
                                          ArchiveReason reason) {
  if (!loaded_ || shut_down_ || !IsRestorableTabUrl(page.url)) {
    return;
  }
  ArchiveSnapshot snap;
  snap.archive_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  snap.space_id = FindSpace(space_id) ? space_id : default_space_id();
  snap.title = page.title;
  snap.url = page.url;
  snap.nav_state = page.nav_state;
  closed_.PushRows({snap.archive_id}, reason);
  std::vector<ArchiveSnapshot> batch;
  batch.push_back(std::move(snap));
  store_.AsyncCall(&CluStore::AddArchive)
      .WithArgs(std::move(batch), reason, NowMs())
      .Then(base::BindOnce(
          [](base::WeakPtr<CluModelService> self,
             std::vector<std::string> written) {
            if (self && !written.empty()) {
              self->BumpArchive();
            }
          },
          weak_ptr_factory_.GetWeakPtr()));
}

void CluModelService::TouchSplit(const std::string& split_id) {
  dirty_splits_.insert(split_id);
}

}  // namespace clu
