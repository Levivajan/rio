// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_model_service.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "clu/browser/model/clu_page_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/ui/webui/bridge/validation.h"
#include "content/public/browser/web_contents.h"

namespace clu {

namespace {

std::string TabCount(size_t n) {
  return n == 1 ? std::string("1 tab")
                : base::StrCat({base::NumberToString(n), " tabs"});
}

const base::TickClock* TickClockOrDefault(const base::TickClock* clock) {
  return clock ? clock : base::DefaultTickClock::GetInstance();
}

}  // namespace

CluModelService::PendingClear::PendingClear() = default;
CluModelService::PendingClear::PendingClear(const PendingClear&) = default;
CluModelService::PendingClear& CluModelService::PendingClear::operator=(
    const PendingClear&) = default;
CluModelService::PendingClear::~PendingClear() = default;

CluModelService::MovedRows::MovedRows() = default;
CluModelService::MovedRows::MovedRows(MovedRows&&) = default;
CluModelService::MovedRows& CluModelService::MovedRows::operator=(
    MovedRows&&) = default;
CluModelService::MovedRows::~MovedRows() = default;

CluModelService::CluModelService(base::FilePath store_path,
                                 const base::Clock* clock,
                                 const base::TickClock* tick_clock)
    : clock_(clock ? clock : base::DefaultClock::GetInstance()),
      store_(base::ThreadPool::CreateSequencedTaskRunner(
                 {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
             std::move(store_path)),
      clear_timer_(TickClockOrDefault(tick_clock)),
      write_timer_(TickClockOrDefault(tick_clock)),
      sweep_timer_(TickClockOrDefault(tick_clock)) {
  store_.AsyncCall(&CluStore::Load)
      .WithArgs(NowMs())
      .Then(base::BindOnce(&CluModelService::OnLoaded,
                           weak_ptr_factory_.GetWeakPtr()));
}

CluModelService::~CluModelService() = default;

void CluModelService::Shutdown() {
  if (shut_down_) {
    return;
  }
  // The toast never ends now: the pending Clear archives, then everything still unwritten goes out. The store's
  // sequence blocks shutdown, so the writes land.
  CommitClear();
  sweep_timer_.Stop();
  clear_timer_.Stop();
  write_timer_.Stop();
  Write();
  shut_down_ = true;
  for (Observer& observer : observers_) {
    observer.OnModelShuttingDown(this);
  }
}

std::string CluModelService::TakeLaunchRow(const std::string& space_id) {
  if (!loaded_ || !launch_rows_taken_.insert(space_id).second) {
    return std::string();
  }
  const std::string& id = tree_.SpaceState(space_id).active_tab_id;
  const TabNode* node = tree_.Get(id);
  return node && node->is_tab() ? id : std::string();
}

void CluModelService::WhenLoaded(base::OnceClosure callback) {
  if (loaded_) {
    std::move(callback).Run();
    return;
  }
  on_loaded_.push_back(std::move(callback));
}

const SpaceRow* CluModelService::FindSpace(std::string_view space_id) const {
  for (const SpaceRow& space : spaces_) {
    if (space.id == space_id) {
      return &space;
    }
  }
  return nullptr;
}

const std::string& CluModelService::default_space_id() const {
  static const base::NoDestructor<std::string> none;
  return spaces_.empty() ? *none : spaces_.front().id;
}

void CluModelService::SetProfileId(std::string profile_id) {
  if (profile_id_ == profile_id) {
    return;
  }
  profile_id_ = std::move(profile_id);
  NotifyChanged();
}

void CluModelService::SetSpaces(std::vector<SpaceRow> spaces) {
  spaces_ = std::move(spaces);
  std::vector<std::string> ids;
  for (const SpaceRow& space : spaces_) {
    ids.push_back(space.id);
  }
  tree_.SetSpaces(ids);
  if (!loaded_ || shut_down_) {
    return;
  }
  if (!ids.empty()) {
    const std::string& first = ids.front();
    const int64_t now = NowMs();
    // Spaces that rows point at but that are not here, each in sidebar order: its pinned tree, then Today.
    std::set<std::string> orphan_spaces;
    for (const TabNode* node : tree_.All()) {
      if (node->section != Section::kFavorite &&
          !tree_.HasSpace(node->space_id)) {
        orphan_spaces.insert(node->space_id);
      }
    }
    std::vector<std::string> orphan_tabs;
    std::vector<std::string> orphan_folders;
    for (const std::string& orphan : orphan_spaces) {
      for (const std::string& id : tree_.Pinned(orphan)) {
        if (const TabNode* node = tree_.Get(id); node && node->is_folder()) {
          orphan_folders.push_back(id);
        }
        for (const std::string& tab : tree_.TabsUnder(id)) {
          orphan_tabs.push_back(tab);
        }
      }
      for (const std::string& id : tree_.Today(orphan)) {
        orphan_tabs.push_back(id);
      }
    }
    for (const std::string& id : orphan_tabs) {
      const int end = static_cast<int>(tree_.Today(first).size());
      if (!tree_.Move(id, DropTarget::Today(first, end), now).has_value()) {
        LOG(WARNING) << "clu: a row of a removed space could not be kept";
      }
    }
    for (const std::string& id : orphan_folders) {
      // Their tabs went to Today above; nested folders went with their parent.
      if (tree_.Get(id)) {
        std::ignore = tree_.DeleteFolder(id, now);
      }
    }
  }
  // A profile left with no space keeps its last mirror: an empty one would make clu.db seed a new "Home" on every
  // load, which a lost Local State would then adopt as a real space.
  if (!spaces_.empty()) {
    store_.AsyncCall(&CluStore::ReplaceSpaces)
        .WithArgs(spaces_, NowMs())
        .Then(base::BindOnce([](bool) {}));
  }
  Commit();
}

void CluModelService::OnLoaded(StoreLoad load) {
  profile_id_ = std::move(load.profile_id);
  persistent_ = load.persistent;
  recovered_ = std::move(load.recovered);
  recovery_toast_pending_ = recovered_.has_value();
  spaces_ = std::move(load.spaces);
  std::vector<std::string> space_ids;
  for (const SpaceRow& space : spaces_) {
    space_ids.push_back(space.id);
  }
  tree_.SetSpaces(std::move(space_ids));
  tree_.Load(std::move(load.tree.nodes), std::move(load.space_states));
  for (SplitRow& split : load.splits) {
    std::string id = split.id;
    splits_.emplace(std::move(id), std::move(split));
  }
  archive_after_hours_ = ArchiveAfterHours(load.archive_after_hours);
  unread_badge_off_ = std::set<std::string>(load.unread_badge_off.begin(),
                                            load.unread_badge_off.end());
  loaded_ = true;
  // F6: the launch sweep runs before the first window shows its rows, and protects each space's restored tab.
  Sweep(/*launch=*/true);
  sweep_timer_.Start(FROM_HERE, base::Milliseconds(kSweepIntervalMs), this,
                     &CluModelService::SweepTick);
  std::vector<base::OnceClosure> callbacks = std::move(on_loaded_);
  for (base::OnceClosure& callback : callbacks) {
    std::move(callback).Run();
  }
  ShowRecoveryToast();
  NotifyChanged();
}

void CluModelService::ShowRecoveryToast() {
  if (!recovery_toast_pending_ || windows_.empty() || !recovered_) {
    return;
  }
  recovery_toast_pending_ = false;
  windows_.front()->LinkToast(
      *recovered_ == "backup"
          ? "Rio couldn’t read its saved data and restored a backup"
          : "Rio couldn’t read its saved data and started fresh",
      std::nullopt);
}

void CluModelService::Commit() {
  write_timer_.Stop();
  Write();
  NotifyChanged();
}

void CluModelService::CommitSoon() {
  if (!write_timer_.IsRunning()) {
    write_timer_.Start(FROM_HERE, kWriteDebounce,
                       base::BindOnce(&CluModelService::Write,
                                      base::Unretained(this)));
  }
  NotifyChanged();
}

void CluModelService::Write() {
  if (shut_down_ || !loaded_) {
    return;
  }
  ValidateSplits();
  TreeChanges changes = tree_.TakeChanges();
  StoreWrite write;
  std::set<std::string> splits = dirty_splits_;
  TakeSplitWrites(write);
  for (const std::string& id : changes.upserts) {
    if (const TabNode* node = tree_.Get(id)) {
      write.upserts.push_back(*node);
    }
  }
  write.deletes.assign(changes.deletes.begin(), changes.deletes.end());
  for (const std::string& space_id : changes.spaces) {
    write.spaces.emplace_back(space_id, tree_.SpaceState(space_id));
  }
  std::vector<StoreWrite::ArchiveBatch> archive = std::move(pending_archive_);
  pending_archive_.clear();
  write.archive = archive;
  if (write.empty()) {
    return;
  }
  store_.AsyncCall(&CluStore::Write)
      .WithArgs(std::move(write))
      .Then(base::BindOnce(&CluModelService::OnWritten,
                           weak_ptr_factory_.GetWeakPtr(), std::move(changes),
                           std::move(splits), std::move(archive)));
}

void CluModelService::OnWritten(TreeChanges changes,
                                std::set<std::string> splits,
                                std::vector<StoreWrite::ArchiveBatch> archive,
                                bool ok) {
  if (ok) {
    write_failures_ = 0;
    if (!archive.empty()) {
      BumpArchive();
    }
    return;
  }
  // The store rolled the batch back (a full disk, an I/O error): nothing of it is on disk, so all of it goes into the
  // next write, ahead of anything archived since.
  tree_.RestoreChanges(changes);
  dirty_splits_.insert(splits.begin(), splits.end());
  archive.insert(archive.end(), std::make_move_iterator(pending_archive_.begin()),
                 std::make_move_iterator(pending_archive_.end()));
  pending_archive_ = std::move(archive);
  if (shut_down_) {
    return;
  }
  write_failures_ = std::min(write_failures_ + 1, 6);
  write_timer_.Start(FROM_HERE, kWriteDebounce * (1 << write_failures_),
                     base::BindOnce(&CluModelService::Write,
                                    base::Unretained(this)));
}

int64_t CluModelService::NowMs() const {
  return clock_->Now().InMillisecondsSinceUnixEpoch();
}

bool CluModelService::SetArchiveAfterHours(double hours) {
  if (!IsArchiveAfterHours(hours)) {
    return false;
  }
  archive_after_hours_ = static_cast<int>(hours);
  store_.AsyncCall(&CluStore::SetSetting)
      .WithArgs(std::string("tabs.archiveAfterHours"),
                base::NumberToString(archive_after_hours_), NowMs())
      .Then(base::BindOnce([](bool) {}));
  NotifyChanged();
  Sweep(/*launch=*/false);
  return true;
}

bool CluModelService::UnreadBadgeShown(const std::string& tab_id) const {
  const TabNode* node = tree_.Get(tab_id);
  return node && node->section == Section::kFavorite &&
         !unread_badge_off_.contains(tab_id);
}

bool CluModelService::SetUnreadBadgeShown(const std::string& tab_id,
                                          bool shown) {
  const TabNode* node = tree_.Get(tab_id);
  if (!node || node->section != Section::kFavorite) {
    return false;
  }
  // Ids of rows that are gone drop out here, so the list stays within the cap.
  std::erase_if(unread_badge_off_,
                [&](const std::string& id) { return !tree_.Get(id); });
  if (shown) {
    unread_badge_off_.erase(tab_id);
  } else if (unread_badge_off_.size() < kMaxUnreadBadgeOff) {
    unread_badge_off_.insert(tab_id);
  }
  base::ListValue ids;
  for (const std::string& id : unread_badge_off_) {
    ids.Append(id);
  }
  store_.AsyncCall(&CluStore::SetSetting)
      .WithArgs(std::string(kUnreadBadgeOffKey),
                base::WriteJson(ids).value_or("[]"), NowMs())
      .Then(base::BindOnce([](bool) {}));
  NotifyChanged();
  return true;
}

void CluModelService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CluModelService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void CluModelService::AddWindow(WindowLink* window) {
  if (!HasWindow(window)) {
    windows_.push_back(window);
  }
  if (loaded_) {
    ShowRecoveryToast();
  }
}

void CluModelService::RemoveWindow(WindowLink* window) {
  std::erase(windows_, window);
  if (pending_) {
    std::erase_if(pending_->active,
                  [&](const auto& entry) { return entry.first == window; });
    if (pending_->window == window) {
      pending_->window = nullptr;
    }
  }
}

bool CluModelService::HasWindow(WindowLink* window) const {
  return window && std::ranges::contains(windows_, window);
}

std::string CluModelService::IdOf(WindowLink* window) {
  return HasWindow(window) ? window->LinkWindowId() : std::string();
}

CluModelService::WindowLink* CluModelService::WindowById(
    const std::string& window_id) {
  for (WindowLink* window : windows_) {
    if (!window_id.empty() && window->LinkWindowId() == window_id) {
      return window;
    }
  }
  return windows_.empty() ? nullptr : windows_.front().get();
}

CluModelService::WindowLink* CluModelService::LiveWindow(WindowLink* window) {
  if (HasWindow(window)) {
    return window;
  }
  return windows_.empty() ? nullptr : windows_.front().get();
}

void CluModelService::NotifyChanged() {
  for (Observer& observer : observers_) {
    observer.OnModelChanged();
  }
}

void CluModelService::BumpArchive() {
  ++archive_revision_;
  NotifyChanged();
}

content::WebContents* CluModelService::FindContents(std::string_view node_id) {
  if (contents_index_) {
    const auto it = contents_index_->find(node_id);
    return it == contents_index_->end() ? nullptr : it->second.get();
  }
  for (WindowLink* window : windows_) {
    if (content::WebContents* contents = window->LinkContents(node_id)) {
      return contents;
    }
  }
  return nullptr;
}

CluModelService::ContentsIndexScope::ContentsIndexScope(CluModelService& model)
    : model_(model), previous_(model.contents_index_) {
  // The first window, and its first page, that has a row wins: FindContents' order.
  for (WindowLink* window : model_->windows_) {
    window->LinkEachContents(
        [this](std::string_view node_id, content::WebContents* contents) {
          index_.try_emplace(std::string(node_id), contents);
        });
  }
  model_->contents_index_ = &index_;
}

CluModelService::ContentsIndexScope::~ContentsIndexScope() {
  model_->contents_index_ = previous_;
}

CluModelService::WindowLink* CluModelService::WindowShowing(
    std::string_view node_id,
    WindowLink* except) {
  for (WindowLink* window : windows_) {
    if (window != except && window->LinkActiveNodeId() == node_id) {
      return window;
    }
  }
  return nullptr;
}

bool CluModelService::IsInSpace(std::string_view node_id,
                                std::string_view space_id) const {
  const TabNode* node = tree_.Get(node_id);
  return node && node->is_tab() &&
         (node->section == Section::kFavorite || node->space_id == space_id);
}

CluModelService::PageCapture::PageCapture() = default;
CluModelService::PageCapture::PageCapture(const PageCapture&) = default;
CluModelService::PageCapture& CluModelService::PageCapture::operator=(
    const PageCapture&) = default;
CluModelService::PageCapture::~PageCapture() = default;

// static
CluModelService::PageCapture CluModelService::Capture(
    content::WebContents* contents) {
  PageCapture capture;
  capture.url = PageUrl(contents);
  capture.title = CleanTitle(base::UTF16ToUTF8(contents->GetTitle()));
  capture.nav_state = CaptureNavState(contents);
  return capture;
}

ArchiveSnapshot CluModelService::SnapshotOf(
    const TabNode& node,
    const std::optional<PageCapture>& closed_page) {
  ArchiveSnapshot snap;
  // The row's own id: a page Chromium restores later carries it (clu_row_stamp.h) and finds this row
  // (ReclaimArchived). A row is archived once; restoring it brings it back under the same id (InsertRestored).
  snap.archive_id = node.id;
  snap.space_id = node.space_id;
  snap.split_id = node.split_id;
  content::WebContents* contents = closed_page ? nullptr : FindContents(node.id);
  if (closed_page) {
    snap.url = closed_page->url;
    snap.title = closed_page->title;
    snap.nav_state = closed_page->nav_state;
  } else if (contents) {
    const PageCapture capture = Capture(contents);
    snap.url = capture.url;
    snap.title = capture.title;
    snap.nav_state = capture.nav_state;
  } else {
    snap.url = node.url;
    snap.title = node.title;
    snap.nav_state = node.nav_state;
  }
  if (!IsRestorableTabUrl(snap.url)) {
    snap.url = node.url;
  }
  if (node.title_override && !node.title_override->empty()) {
    snap.title = *node.title_override;
  }
  return snap;
}

std::optional<std::string> CluModelService::NextActiveFor(
    WindowLink* window,
    const RemovedRow& removed,
    const std::set<std::string>& gone) {
  const std::string space_id = window->LinkSpaceId();
  const bool same_space = removed.node.section == Section::kFavorite ||
                          removed.node.space_id == space_id;
  std::vector<std::string> siblings;
  if (same_space) {
    for (const std::string& id : removed.siblings) {
      const TabNode* node = tree_.Get(id);
      if (id == removed.node.id || (node && node->is_tab())) {
        siblings.push_back(id);
      }
    }
  }
  return NextActive(siblings, removed.node.id, gone, window->LinkMru(),
                    [&](std::string_view id) {
                      return IsInSpace(id, space_id) &&
                             !pending_ids_.contains(std::string(id)) &&
                             !WindowShowing(id, window);
                    });
}

void CluModelService::DropPages(const std::vector<std::string>& ids) {
  const std::vector<raw_ptr<WindowLink>> windows = windows_;
  for (WindowLink* window : windows) {
    if (!HasWindow(window)) {
      continue;
    }
    for (const std::string& id : ids) {
      window->LinkDropPage(id);
    }
    window->LinkRowsRemoved(ids);
  }
}

void CluModelService::DeleteSpaceRows(const std::string& space_id) {
  CommitClear();
  std::vector<std::string> tabs;
  for (const std::string& id : tree_.Pinned(space_id)) {
    for (const std::string& tab : tree_.TabsUnder(id)) {
      tabs.push_back(tab);
    }
  }
  for (const std::string& id : tree_.Today(space_id)) {
    tabs.push_back(id);
  }
  ArchiveRows(tabs, ArchiveReason::kDelete);
  // What is left is folders and the space's state.
  std::ignore = tree_.TakeSpace(space_id, nullptr);
  Commit();
}

CluModelService::MovedRows CluModelService::TakeSpaceRows(
    const std::string& space_id) {
  CommitClear();
  MovedRows rows;
  rows.nodes = tree_.TakeSpace(space_id, &rows.state);
  std::vector<std::string> tabs;
  for (const TabNode& node : rows.nodes) {
    if (node.is_tab()) {
      tabs.push_back(node.id);
      reopen_overrides_.erase(node.id);
    }
  }
  Commit();
  DropPages(tabs);
  return rows;
}

void CluModelService::AddSpaceRows(const std::string& space_id,
                                   MovedRows rows) {
  for (TabNode& node : rows.nodes) {
    // F8: the page comes back in this profile at the row's URL, without history.
    node.space_id = node.section == Section::kFavorite ? std::string()
                                                       : space_id;
    node.nav_state.reset();
  }
  ReturnSpaceRows(space_id, std::move(rows));
}

void CluModelService::ReturnSpaceRows(const std::string& space_id,
                                      MovedRows rows) {
  tree_.AddNodes(std::move(rows.nodes));
  if (tree_.Get(rows.state.active_tab_id) == nullptr) {
    rows.state.active_tab_id.clear();
  }
  tree_.SetSpaceState(space_id, std::move(rows.state));
  Commit();
}

CluModelService::MovedRows CluModelService::TakeSubtree(
    const std::string& node_id) {
  CommitClear();
  MovedRows rows;
  rows.nodes = tree_.TakeSubtree(node_id);
  std::vector<std::string> tabs;
  for (const TabNode& node : rows.nodes) {
    if (node.is_tab()) {
      tabs.push_back(node.id);
      reopen_overrides_.erase(node.id);
    }
  }
  Commit();
  DropPages(tabs);
  return rows;
}

CluModelService::MovedRows CluModelService::CopySubtree(
    const std::string& node_id) const {
  MovedRows rows;
  const TabNode* root = tree_.Get(node_id);
  if (!root) {
    return rows;
  }
  // Pre-order, as CluTabTree::TakeSubtree() gives them: a folder before what it holds.
  std::vector<const TabNode*> stack = {root};
  while (!stack.empty()) {
    const TabNode* node = stack.back();
    stack.pop_back();
    rows.nodes.push_back(*node);
    if (node->is_folder()) {
      const std::vector<std::string>& children = tree_.Children(node->id);
      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (const TabNode* child = tree_.Get(*it)) {
          stack.push_back(child);
        }
      }
    }
  }
  return rows;
}

base::expected<void, TreeError> CluModelService::CanAddSubtree(
    const MovedRows& rows,
    const DropTarget& target) const {
  if (rows.nodes.empty()) {
    return base::unexpected(TreeError::kNotFound);
  }
  const TabNode& root = rows.nodes.front();
  // Every row must be one this tree can take as it is: a new id and, for a tab, a URL a row may hold. Otherwise an
  // insert would fail after the rows left the other profile.
  for (const TabNode& node : rows.nodes) {
    if (tree_.Get(node.id)) {
      return base::unexpected(TreeError::kBadTarget);
    }
    if (node.is_tab() && !IsRestorableTabUrl(node.url) &&
        !(node.pinned_url && IsRestorableTabUrl(*node.pinned_url))) {
      return base::unexpected(TreeError::kBadUrl);
    }
  }
  if (target.section == Section::kFavorite) {
    if (root.is_folder()) {
      return base::unexpected(TreeError::kFolderNotAllowed);
    }
    if (tree_.Favorites().size() >= static_cast<size_t>(kMaxFavorites)) {
      return base::unexpected(TreeError::kFavoritesFull);
    }
    return base::ok();
  }
  if (!tree_.HasSpace(target.space_id)) {
    return base::unexpected(TreeError::kBadTarget);
  }
  if (root.is_folder() && target.section != Section::kPinned) {
    return base::unexpected(TreeError::kFolderNotAllowed);
  }
  if (!target.parent_id.empty()) {
    const TabNode* parent = tree_.Get(target.parent_id);
    if (!parent || !parent->is_folder() ||
        parent->space_id != target.space_id) {
      return base::unexpected(TreeError::kBadTarget);
    }
  }
  if (root.is_folder()) {
    // Folder levels the subtree brings: the root is 1, each nested folder adds one.
    std::map<std::string, int> level;
    int height = 0;
    for (const TabNode& node : rows.nodes) {
      if (!node.is_folder()) {
        continue;
      }
      const auto parent = level.find(node.parent_id);
      const int at = parent == level.end() ? 1 : parent->second + 1;
      level[node.id] = at;
      height = std::max(height, at);
    }
    const int base_depth =
        target.parent_id.empty() ? 0 : tree_.Depth(target.parent_id);
    if (base_depth + height > kMaxFolderDepth) {
      return base::unexpected(TreeError::kTooDeep);
    }
  }
  return base::ok();
}

base::expected<void, TreeError> CluModelService::AddSubtree(
    MovedRows rows,
    const DropTarget& target) {
  base::expected<void, TreeError> allowed = CanAddSubtree(rows, target);
  if (!allowed.has_value()) {
    return allowed;
  }
  const int64_t now = NowMs();
  const std::string root_id = rows.nodes.front().id;
  // Each node goes in through the tree's own inserts, so the F8 table sets pinned URLs and the Today clock.
  for (const TabNode& node : rows.nodes) {
    const bool root = node.id == root_id;
    DropTarget at = target;
    if (!root) {
      at = DropTarget::Pinned(target.space_id, node.parent_id,
                              static_cast<int>(
                                  tree_.Children(node.parent_id).size()));
    }
    if (node.is_folder()) {
      std::ignore = tree_.CreateFolder(
          at.space_id, at.parent_id,
          node.title_override.value_or(kDefaultFolderName), at.index, now,
          node.id);
      if (TabNode* folder = tree_.Mutable(node.id)) {
        folder->collapsed = node.collapsed;
        folder->custom_icon = node.custom_icon;
      }
      continue;
    }
    NewTab init;
    init.id = node.id;
    // A row that stays anchored keeps its pinned URL (the F8 table), not the page it wandered to.
    const bool stays_anchored = at.section != Section::kToday;
    init.url = stays_anchored && node.pinned_url &&
                       IsRestorableTabUrl(*node.pinned_url)
                   ? *node.pinned_url
                   : node.url;
    if (!IsRestorableTabUrl(init.url) && node.pinned_url) {
      init.url = *node.pinned_url;
    }
    init.title = node.title;
    init.muted = node.muted;
    init.last_active_at = now;
    if (!tree_.InsertTab(init, at, now).has_value()) {
      continue;
    }
    if (TabNode* tab = tree_.Mutable(node.id)) {
      tab->title_override = node.title_override;
      if (tab->section == Section::kPinned) {
        tab->custom_icon = node.custom_icon;
      }
    }
  }
  Commit();
  return base::ok();
}

void CluModelService::FallBack(const std::vector<RemovedRow>& removed) {
  std::set<std::string> gone;
  std::vector<std::string> ids;
  for (const RemovedRow& row : removed) {
    gone.insert(row.node.id);
    ids.push_back(row.node.id);
  }
  // Copied: a window may close while it activates.
  const std::vector<raw_ptr<WindowLink>> windows = windows_;
  for (WindowLink* window : windows) {
    if (!HasWindow(window)) {
      continue;
    }
    const std::string active = window->LinkActiveNodeId();
    const auto it = std::ranges::find_if(
        removed, [&](const RemovedRow& row) { return row.node.id == active; });
    if (it != removed.end()) {
      window->LinkActivate(NextActiveFor(window, *it, gone));
    }
  }
  for (WindowLink* window : windows) {
    if (!HasWindow(window)) {
      continue;
    }
    for (const std::string& id : ids) {
      window->LinkDropPage(id);
    }
    window->LinkRowsRemoved(ids);
  }
}

void CluModelService::CloseRow(const std::string& node_id,
                               std::optional<PageCapture> closed_page) {
  const TabNode* node = tree_.Get(node_id);
  if (!node || !node->is_tab()) {
    return;
  }
  if (!node->anchored()) {
    ArchiveRowsWith({node_id}, ArchiveReason::kClose, closed_page);
    return;
  }
  // F9.5: the row stays, loses its page and history, and reads as its pinned URL again. Cmd+Shift+T brings back the
  // last URL and history.
  RemovedRow row;
  row.node = *node;
  row.siblings = tree_.ListOf(*node);
  if (!closed_page) {
    if (content::WebContents* contents = FindContents(node_id)) {
      closed_page = Capture(contents);
    }
  }
  closed_.PushAnchored(node_id, closed_page ? closed_page->url : node->url,
                       closed_page ? closed_page->nav_state : std::nullopt);
  TabNode* mutable_node = tree_.Mutable(node_id);
  mutable_node->url = mutable_node->pinned_url.value_or(mutable_node->url);
  mutable_node->nav_state.reset();
  Commit();
  FallBack({row});
}

void CluModelService::ArchiveRows(const std::vector<std::string>& node_ids,
                                  ArchiveReason reason) {
  ArchiveRowsWith(node_ids, reason, std::nullopt);
}

void CluModelService::ArchiveRowsWith(
    const std::vector<std::string>& node_ids,
    ArchiveReason reason,
    const std::optional<PageCapture>& closed_page) {
  std::vector<std::string> ids;
  for (const std::string& id : node_ids) {
    const TabNode* node = tree_.Get(id);
    if (node && node->is_tab() && !std::ranges::contains(ids, id)) {
      ids.push_back(id);
    }
  }
  if (ids.empty()) {
    return;
  }
  std::vector<ArchiveSnapshot> today;
  std::vector<ArchiveSnapshot> anchored;
  for (const std::string& id : ids) {
    const TabNode* node = tree_.Get(id);
    // A page that already closed is the only row of its batch.
    (node->anchored() ? anchored : today)
        .push_back(SnapshotOf(*node, closed_page));
  }
  const int64_t now = NowMs();
  for (auto& [batch, why] :
       {std::make_pair(&today, reason),
        std::make_pair(&anchored, ArchiveReason::kDelete)}) {
    if (batch->empty()) {
      continue;
    }
    std::vector<std::string> archive_ids;
    for (const ArchiveSnapshot& snap : *batch) {
      archive_ids.push_back(snap.archive_id);
    }
    closed_.PushRows(std::move(archive_ids), why);
    // The archive rows go out with the tree's write below, in the transaction that deletes the tabs.
    pending_archive_.emplace_back(std::move(*batch), why, now);
  }
  std::vector<RemovedRow> removed;
  for (const std::string& id : ids) {
    pending_ids_.erase(id);
    if (std::optional<RemovedRow> row = tree_.RemoveTab(id)) {
      removed.push_back(std::move(*row));
    }
  }
  ++archive_revision_;
  Commit();
  FallBack(removed);
}

int CluModelService::ClearToday(const std::string& space_id,
                                WindowLink* window) {
  CommitClear();
  if (!tree_.HasSpace(space_id)) {
    return 0;
  }
  const std::vector<std::string> ids = tree_.Today(space_id);
  if (ids.empty()) {
    return 0;
  }
  PendingClear pending;
  pending.space_id = space_id;
  pending.window = window;
  pending.ids = ids;
  for (WindowLink* w : windows_) {
    const std::string active = w->LinkActiveNodeId();
    if (std::ranges::contains(ids, active)) {
      pending.active.emplace_back(w, active);
    }
  }
  pending_ = std::move(pending);
  pending_ids_ = std::set<std::string>(ids.begin(), ids.end());

  // Windows that showed a cleared tab show the space's most recent pinned tab no other window shows, else nothing.
  // A copy: a window that closes while it activates leaves pending_->active.
  const auto active_windows = pending_->active;
  for (const auto& [w, tab_id] : active_windows) {
    if (!HasWindow(w)) {
      continue;
    }
    std::vector<const TabNode*> pinned;
    for (const TabNode* node : tree_.All()) {
      if (node->is_tab() && node->section == Section::kPinned &&
          node->space_id == space_id && !WindowShowing(node->id, w)) {
        pinned.push_back(node);
      }
    }
    std::ranges::sort(pinned, [](const TabNode* a, const TabNode* b) {
      return a->last_active_at > b->last_active_at;
    });
    w->LinkActivate(pinned.empty() ? std::nullopt
                                   : std::optional<std::string>(pinned[0]->id));
  }
  clear_timer_.Start(FROM_HERE, kClearUndo,
                     base::BindOnce(&CluModelService::CommitClear,
                                    base::Unretained(this)));
  if (HasWindow(window)) {
    window->LinkToast(base::StrCat({"Cleared ", TabCount(ids.size())}),
                      "archive.undoClear");
  }
  NotifyChanged();
  return static_cast<int>(ids.size());
}

bool CluModelService::UndoClear() {
  if (!pending_) {
    return false;
  }
  PendingClear p = std::move(*pending_);
  pending_.reset();
  pending_ids_.clear();
  clear_timer_.Stop();
  // Tabs that left meanwhile (closed, moved) stay gone.
  std::vector<std::string> back;
  for (const std::string& id : p.ids) {
    const TabNode* node = tree_.Get(id);
    if (node && node->section == Section::kToday &&
        node->space_id == p.space_id) {
      back.push_back(id);
    }
  }
  for (const auto& [w, tab_id] : p.active) {
    if (HasWindow(w) && w->LinkSpaceId() == p.space_id &&
        std::ranges::contains(back, tab_id)) {
      w->LinkActivate(tab_id);
    }
  }
  if (WindowLink* window = HasWindow(p.window) ? p.window.get() : nullptr) {
    window->LinkDropToasts("archive.undoClear");
    window->LinkToast(base::StrCat({"Restored ", TabCount(back.size())}),
                      std::nullopt);
  }
  NotifyChanged();
  return true;
}

void CluModelService::CommitClear() {
  if (!pending_) {
    return;
  }
  PendingClear p = std::move(*pending_);
  pending_.reset();
  pending_ids_.clear();
  clear_timer_.Stop();
  // A cleared tab some window shows again goes back on the list instead.
  std::vector<std::string> ids;
  for (const std::string& id : p.ids) {
    if (tree_.Get(id) && !WindowShowing(id, nullptr)) {
      ids.push_back(id);
    }
  }
  if (ids.empty()) {
    NotifyChanged();
    return;
  }
  ArchiveRows(ids, ArchiveReason::kClear);
}

std::optional<std::pair<std::string, std::optional<std::string>>>
CluModelService::TakeReopenOverride(std::string_view node_id) {
  const auto it = reopen_overrides_.find(node_id);
  if (it == reopen_overrides_.end()) {
    return std::nullopt;
  }
  auto value = std::move(it->second);
  reopen_overrides_.erase(it);
  return value;
}

void CluModelService::ReopenClosed(WindowLink* window,
                                   base::OnceCallback<void(bool)> done) {
  // A Clear still showing its Undo is the newest close: Cmd+Shift+T undoes it.
  if (pending_) {
    std::move(done).Run(UndoClear());
    return;
  }
  ReopenNext(window, std::move(done));
}

void CluModelService::ReopenNext(WindowLink* window,
                                 base::OnceCallback<void(bool)> done) {
  for (std::optional<ClosedEntry> entry = closed_.Pop(); entry;
       entry = closed_.Pop()) {
    if (entry->kind == ClosedEntry::Kind::kRows) {
      store_.AsyncCall(&CluStore::TakeArchive)
          .WithArgs(entry->archive_ids)
          .Then(base::BindOnce(&CluModelService::OnReopenRows,
                               weak_ptr_factory_.GetWeakPtr(), IdOf(window),
                               /*from_stack=*/true, std::move(done)));
      return;
    }
    // An anchored close: the row comes back with its last URL and history. A row since removed or unpinned is stale.
    const TabNode* node = tree_.Get(entry->tab_id);
    WindowLink* target = LiveWindow(window);
    if (!node || !node->is_tab() || !node->anchored() || !target) {
      continue;
    }
    if (!FindContents(entry->tab_id)) {
      reopen_overrides_[entry->tab_id] =
          std::make_pair(entry->url, entry->nav_state);
    }
    target->LinkActivate(entry->tab_id);
    std::move(done).Run(true);
    return;
  }
  // The stack is empty (after a restart): the newest reopenable archive row or batch, never an auto row.
  store_.AsyncCall(&CluStore::TakeNewestReopenable)
      .Then(base::BindOnce(&CluModelService::OnReopenRows,
                           weak_ptr_factory_.GetWeakPtr(), IdOf(window),
                           /*from_stack=*/false, std::move(done)));
}

void CluModelService::OnReopenRows(const std::string& window_id,
                                   bool from_stack,
                                   base::OnceCallback<void(bool)> done,
                                   std::vector<ArchiveRow> rows) {
  WindowLink* window = WindowById(window_id);
  if (rows.empty()) {
    // Every row of that entry was since restored, deleted or pruned: try the next one.
    if (from_stack) {
      ReopenNext(window, std::move(done));
    } else {
      std::move(done).Run(false);
    }
    return;
  }
  RestoreRows(std::move(rows), window, std::move(done));
}

std::optional<std::string> CluModelService::InsertRestored(
    const ArchiveRow& row,
    WindowLink* window) {
  // A row goes back to its own space while that space exists, else to the window's space (02-tabs F7).
  std::string space_id = row.space_id;
  if (!FindSpace(space_id)) {
    space_id = window ? window->LinkSpaceId() : default_space_id();
  }
  if (!FindSpace(space_id)) {
    space_id = default_space_id();
  }
  NewTab init;
  // The row keeps the id its archive row has (the closed row's id) while no row holds it, so a copy of the page
  // Chromium restores later finds this row instead of making a second one.
  if (!tree_.Get(row.id)) {
    init.id = row.id;
  }
  init.url = row.url;
  init.title = row.title;
  init.nav_state = row.nav_state;
  const int64_t now = NowMs();
  init.last_active_at = now;
  base::expected<std::string, TreeError> id =
      tree_.InsertTab(init, DropTarget::Today(space_id, 0), now);
  if (!id.has_value()) {
    return std::nullopt;
  }
  return id.value();
}

void CluModelService::RestoreRows(std::vector<ArchiveRow> rows,
                                  WindowLink* window,
                                  base::OnceCallback<void(bool)> done) {
  window = LiveWindow(window);
  std::vector<ArchiveRow> ok;
  bool refused = false;
  for (ArchiveRow& row : rows) {
    if (IsRestorableTabUrl(row.url)) {
      ok.push_back(std::move(row));
    } else {
      refused = true;
    }
  }
  // Each lands at the top of Today, so the last goes in first and the batch
  // keeps its order; the first one shows. A split group comes back as a split
  // (PLAN decision 55).
  std::optional<std::string> first;
  std::vector<std::string> restored_archive_ids;
  std::map<std::string, std::vector<std::string>> groups;
  for (auto it = ok.rbegin(); it != ok.rend(); ++it) {
    if (std::optional<std::string> id = InsertRestored(*it, window)) {
      first = id;
      restored_archive_ids.push_back(it->id);
      if (it->group_id) {
        std::vector<std::string>& members = groups[*it->group_id];
        members.insert(members.begin(), *id);
      }
    }
  }
  for (const auto& [group_id, members] : groups) {
    if (members.size() >= 2) {
      std::ignore =
          CreateSplit({members[0], members[1]}, SplitDir::kRow, members[0]);
    }
  }
  Commit();
  if (first && window) {
    window->LinkActivate(first);
  }
  // The store took the rows out of the archive when it read them (CluStore::TakeArchive()).
  if (!restored_archive_ids.empty()) {
    BumpArchive();
  }
  if (refused && window) {
    window->LinkToast("This page can't be restored", std::nullopt);
  }
  std::move(done).Run(first.has_value() || refused);
}

void CluModelService::ReclaimArchived(const std::string& row_id,
                                      base::OnceCallback<void(bool)> done) {
  if (!loaded_ || shut_down_ || !IsUuidV4(row_id)) {
    std::move(done).Run(false);
    return;
  }
  store_.AsyncCall(&CluStore::TakeArchive)
      .WithArgs(std::vector<std::string>{row_id})
      .Then(base::BindOnce(
          [](base::WeakPtr<CluModelService> self, const std::string& row_id,
             base::OnceCallback<void(bool)> done,
             std::vector<ArchiveRow> rows) {
            if (!self || rows.empty()) {
              std::move(done).Run(false);
              return;
            }
            self->BumpArchive();
            const TabNode* node = self->tree_.Get(row_id);
            const std::string& space_id = rows[0].space_id;
            if (node && node->is_tab() && node->section == Section::kToday &&
                node->space_id != space_id && self->FindSpace(space_id) &&
                self->tree_
                    .Move(row_id, DropTarget::Today(space_id, 0),
                          self->NowMs())
                    .has_value()) {
              self->Commit();
            }
            std::move(done).Run(true);
          },
          weak_ptr_factory_.GetWeakPtr(), row_id, std::move(done)));
}

void CluModelService::RestoreArchive(
    const std::string& archive_id,
    WindowLink* window,
    base::OnceCallback<void(std::optional<std::string>)> done) {
  store_.AsyncCall(&CluStore::TakeArchive)
      .WithArgs(std::vector<std::string>{archive_id})
      .Then(base::BindOnce(
          [](base::WeakPtr<CluModelService> self, const std::string& window_id,
             base::OnceCallback<void(std::optional<std::string>)> done,
             std::vector<ArchiveRow> rows) {
            if (!self || rows.empty()) {
              std::move(done).Run("not-found");
              return;
            }
            if (!IsRestorableTabUrl(rows[0].url)) {
              // The row stays so it can be deleted (02-tabs F7).
              if (WindowLink* w = self->WindowById(window_id)) {
                w->LinkToast("This page can't be restored", std::nullopt);
              }
              std::move(done).Run("not-restorable");
              return;
            }
            self->RestoreRows(std::move(rows), self->WindowById(window_id),
                              base::BindOnce(
                                  [](base::OnceCallback<void(
                                         std::optional<std::string>)> done,
                                     bool restored) {
                                    std::move(done).Run(
                                        restored ? std::nullopt
                                                 : std::optional<std::string>(
                                                       "not-found"));
                                  },
                                  std::move(done)));
          },
          weak_ptr_factory_.GetWeakPtr(), IdOf(window), std::move(done)));
}

void CluModelService::DeleteArchive(const std::string& archive_id,
                                    base::OnceCallback<void(bool)> done) {
  store_.AsyncCall(&CluStore::RemoveArchive)
      .WithArgs(std::vector<std::string>{archive_id})
      .Then(base::BindOnce(
          [](base::WeakPtr<CluModelService> self,
             base::OnceCallback<void(bool)> done, int removed) {
            if (self && removed > 0) {
              self->BumpArchive();
            }
            std::move(done).Run(removed > 0);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(done)));
}

void CluModelService::ClearArchiveBetween(int64_t begin,
                                          int64_t end,
                                          base::OnceCallback<void(int)> done) {
  store_.AsyncCall(&CluStore::RemoveArchiveBetween)
      .WithArgs(begin, end)
      .Then(base::BindOnce(
          [](base::WeakPtr<CluModelService> self,
             base::OnceCallback<void(int)> done, int removed) {
            if (self && removed > 0) {
              self->BumpArchive();
            }
            std::move(done).Run(removed);
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(done)));
}

std::vector<std::string> CluModelService::AddImportedRows(
    std::vector<TabNode> nodes) {
  if (!loaded_ || shut_down_ || nodes.empty()) {
    return {};
  }
  std::vector<std::string> added = tree_.AddNodes(std::move(nodes));
  if (!added.empty()) {
    Commit();
  }
  return added;
}

void CluModelService::GetImportMap(
    std::string source,
    std::vector<std::string> keys,
    base::OnceCallback<void(std::map<std::string, std::string>)> done) {
  store_.AsyncCall(&CluStore::GetImportMap)
      .WithArgs(std::move(source), std::move(keys))
      .Then(std::move(done));
}

void CluModelService::PutImportMap(
    std::string source,
    std::vector<std::pair<std::string, std::string>> rows) {
  store_.AsyncCall(&CluStore::PutImportMap)
      .WithArgs(std::move(source), std::move(rows))
      .Then(base::BindOnce([](bool) {}));
}

void CluModelService::ListArchive(const std::string& query,
                                  const std::string& cursor,
                                  int limit,
                                  base::OnceCallback<void(ArchivePage)> done) {
  store_.AsyncCall(&CluStore::ListArchive)
      .WithArgs(query, cursor, limit)
      .Then(std::move(done));
}

void CluModelService::SweepTick() {
  Sweep(/*launch=*/false);
}

std::vector<std::string> CluModelService::Sweep(bool launch) {
  if (!loaded_ || shut_down_) {
    return {};
  }
  const int64_t now = NowMs();
  std::set<std::string> active;
  for (WindowLink* window : windows_) {
    active.insert(window->LinkActiveNodeId());
  }
  if (launch) {
    for (const std::string& space_id : tree_.spaces()) {
      active.insert(tree_.SpaceState(space_id).active_tab_id);
    }
  }
  std::vector<SweepTab> tabs;
  for (const TabNode* node : tree_.All()) {
    if (!node->is_tab() || node->section != Section::kToday ||
        pending_ids_.contains(node->id)) {
      continue;
    }
    SweepTab tab;
    tab.id = node->id;
    tab.section = node->section;
    tab.last_active_at = node->last_active_at;
    tab.split_id = node->split_id;
    tab.held = active.contains(node->id);
    if (!tab.held) {
      if (content::WebContents* contents = FindContents(node->id)) {
        tab.held = contents->IsCurrentlyAudible() || contents->IsBeingCaptured();
      }
    }
    tabs.push_back(std::move(tab));
  }
  std::vector<std::string> ids =
      SelectIdleTabs(tabs, now, ArchiveAfterMs(archive_after_hours_),
                     sweep_first_seen_);
  if (!ids.empty()) {
    ArchiveRows(ids, ArchiveReason::kAuto);
  }
  store_.AsyncCall(&CluStore::Prune)
      .WithArgs(now)
      .Then(base::BindOnce(
          [](base::WeakPtr<CluModelService> self, int removed) {
            if (self && removed > 0) {
              self->BumpArchive();
            }
          },
          weak_ptr_factory_.GetWeakPtr()));
  return ids;
}

void CluModelService::SetClockForTesting(const base::Clock* clock) {
  clock_ = clock ? clock : base::DefaultClock::GetInstance();
}

void CluModelService::SetStoreFailWritesForTesting(bool fail) {
  store_.AsyncCall(&CluStore::SetFailWritesForTesting).WithArgs(fail);
}

void CluModelService::FlushForTesting(base::OnceClosure done) {
  write_timer_.Stop();
  Write();
  store_.AsyncCall(&CluStore::Noop).Then(std::move(done));
}

}  // namespace clu
