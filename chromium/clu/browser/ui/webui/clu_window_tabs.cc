// Copyright 2026 The Rio Authors. All rights reserved.

// CluWindowState's rows and pages (docs/plan/02-tabs.md through docs/chromium/m3.md): how the model's rows map to
// this window's TabStripModel. The strip notifies synchronously and must not change while it does, so anything that
// changes the strip in answer to a notification runs from a posted task.

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/back_forward_menu_model.h"
#include "clu/browser/ai/clu_ai_client.h"
#include "clu/browser/ai/clu_shift_key.h"
#include "clu/browser/model/clu_page_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "clu/browser/model/clu_tidy.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/ui/webui/clu_live_folders.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_tab_menu.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_sidebar_peek.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/common/referrer.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/webview/webview.h"
#include "url/gurl.h"

namespace clu {

namespace {

void PostToSelf(base::WeakPtr<CluWindowState> self,
                base::OnceCallback<void(CluWindowState*)> task) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<CluWindowState> self,
             base::OnceCallback<void(CluWindowState*)> task) {
            if (self) {
              std::move(task).Run(self.get());
            }
          },
          std::move(self), std::move(task)));
}

// A user's mute, as the tab strip's audio icon sets it: a plain WebContents::SetAudioMuted() is undone by
// SoundContentSettingObserver on the page's next navigation.
void SetPageMuted(content::WebContents* contents, bool muted) {
  SetTabAudioMuted(contents, muted, TabMutedReason::kAudioIndicator,
                   std::string());
}

// The URL pill's and the toolbar field's right-click menu: one item that turns toolbar mode on or off.
class CluToolbarMenuModel : public ui::SimpleMenuModel,
                            public ui::SimpleMenuModel::Delegate {
 public:
  explicit CluToolbarMenuModel(base::WeakPtr<CluWindowState> window)
      : ui::SimpleMenuModel(this), window_(std::move(window)) {
    const bool shown = window_ && window_->shell().chrome_state().toolbar;
    AddItem(kToggle, shown ? u"Hide Toolbar" : u"Show as Toolbar");
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kToggle && window_) {
      window_->shell().ToggleToolbar();
    }
  }

 private:
  static constexpr int kToggle = 1;
  base::WeakPtr<CluWindowState> window_;
};

}  // namespace

// Model link ----------------------------------------------------------------------------------------------------------

void CluWindowState::OnModelLoaded() {
  if (!model_ || !model_->loaded()) {
    return;
  }
  model_ready_ = true;
  profile_id_ = model_->profile_id();
  // Live folders read their feeds once the first window's rows are here, then every 30 minutes.
  CluLiveFolders::Get().EnsureStarted();
  // A window a cross-profile switch made opens on the space it was made for (docs/chromium/m4.md).
  const bool for_space =
      initial_space_.has_value() && model_->FindSpace(*initial_space_);
  SettleSpace();
  std::vector<std::pair<std::string, int64_t>> seeds;
  for (const TabNode* node : model_->tree().All()) {
    if (node->is_tab()) {
      seeds.emplace_back(node->id, node->last_active_at);
    }
  }
  mru_.SeedIfEmpty(seeds);
  // Pages the strip got before the rows were read become rows (or placeholders).
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    AdoptPage(strip->GetWebContentsAt(i));
  }
  if (for_space) {
    initial_space_.reset();
    ActivateSpaceTarget();
  } else {
    // A window opened for a row (Move Tab to New Window) shows that row's space.
    if (tabs::TabInterface* tab = ActiveTab()) {
      const TabNode* node =
          model_->tree().Get(CluTabId::Get(tab->GetContents()));
      if (node && !node->space_id.empty() &&
          model_->FindSpace(node->space_id)) {
        space_id_ = node->space_id;
      }
    }
    if (!ActiveTab()) {
      RestoreOrEmpty();
    }
  }
  if (CluSpacesService* spaces = CluSpacesService::Get();
      spaces && model_->FindSpace(space_id_)) {
    spaces->SetLastSpace(space_id_);
  }
  UpdatePageShown();
  MarkDirty();
  NotifyFrameReady();
}

void CluWindowState::RestoreOrEmpty() {
  CluModelService* m = model();
  if (!m || ActiveTab()) {
    RemovePlaceholderIfUnused();
    return;
  }
  // The first window of the run shows its space's restored row; only that page loads.
  const std::string launch = m->TakeLaunchRow(space_id_);
  if (!launch.empty() && ActivateRow(launch)) {
    return;
  }
  // A real page of this window (startup's new-tab page went in front of it).
  for (const std::string& id : mru_.list()) {
    if (FindTab(id)) {
      ActivateRow(id);
      return;
    }
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    if (!CluPlaceholder::Is(strip->GetWebContentsAt(i))) {
      ActivateTab(strip->GetTabAtIndex(i));
      return;
    }
  }
  ShowEmpty();
}

void CluWindowState::AdoptPage(content::WebContents* contents) {
  CluModelService* m = model();
  if (!m || CluPlaceholder::Is(contents)) {
    return;
  }
  if (IsNewTabPageContents(contents)) {
    // Clu has no new-tab page; the window shows its restored row or the empty state instead.
    CluPlaceholder::Mark(contents);
    PostToSelf(GetWeakPtr(),
               base::BindOnce([](CluWindowState* self) {
                 self->RestoreOrEmpty();
               }));
    return;
  }
  const std::string id = CluTabId::Get(contents);
  const bool restored = CluTabId::TakeRestored(contents);
  if (const TabNode* row = m->tree().Get(id)) {
    // Chromium brought the page back (session restore, Recently Closed, "Continue where you left off") with the id of
    // a row that is still here: it is that row's page again. When the row already shows a page, this one is a copy:
    // it stops naming the row at once and goes once the strip settles.
    if (restored) {
      if (RowShowsElsewhere(id, contents)) {
        CluTabId::Set(contents,
                      base::Uuid::GenerateRandomV4().AsLowercaseString());
        PostToSelf(GetWeakPtr(),
                   base::BindOnce(
                       [](base::WeakPtr<content::WebContents> copy,
                          std::string row_id, CluWindowState* self) {
                         self->DropRestoredCopy(copy.get(), row_id);
                       },
                       contents->GetWeakPtr(), id));
      } else {
        SetPageMuted(contents, row->muted);
        ShowRestoredRowSoon(contents);
      }
    }
    return;
  }
  // A page Clu did not open (a link with target=_blank, an extension, the History menu, a page typed into the bar): a
  // Today row at the top of the space, under New Tab, as every new tab is, never under its opener.
  NewTab init;
  init.id = id;
  const std::string url = PageUrl(contents);
  init.url = IsRestorableTabUrl(url) ? url : std::string("about:blank");
  init.title = base::UTF16ToUTF8(contents->GetTitle());
  init.muted = contents->IsAudioMuted();
  if (m->mutable_tree()
          .InsertTab(init, DropTarget::Today(space_id_, 0), m->NowMs())
          .has_value()) {
    m->Commit();
    // A restored page whose row was archived (a closed Today row): the row goes back to its own space and leaves the
    // archive, so the page is one row, never a row and an archive entry.
    if (restored) {
      m->ReclaimArchived(
          id, base::BindOnce(
                  [](base::WeakPtr<CluWindowState> self,
                     base::WeakPtr<content::WebContents> page, bool reclaimed) {
                    if (self && page && reclaimed) {
                      self->ShowRestoredRowSoon(page.get());
                    }
                  },
                  GetWeakPtr(), contents->GetWeakPtr()));
    }
  }
}

bool CluWindowState::RowShowsElsewhere(const std::string& row_id,
                                       content::WebContents* except) {
  for (BrowserWindowInterface* browser : GetAllBrowserWindowInterfaces()) {
    if (browser->GetProfile() != profile()) {
      continue;
    }
    TabStripModel* strip = browser->GetTabStripModel();
    for (int i = 0; strip && i < strip->count(); ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      if (contents != except && !CluPlaceholder::Is(contents) &&
          CluTabId::Has(contents) && CluTabId::Get(contents) == row_id) {
        return true;
      }
    }
  }
  return false;
}

void CluWindowState::ShowRestoredRowSoon(content::WebContents* contents) {
  PostToSelf(GetWeakPtr(),
             base::BindOnce(
                 [](base::WeakPtr<content::WebContents> page,
                    CluWindowState* self) {
                   // Chromium shows the page it restored: the window goes to that row's space, as a click on it
                   // would.
                   tabs::TabInterface* active = self->ActiveTab();
                   if (page && active && active->GetContents() == page.get() &&
                       CluTabId::Has(page.get())) {
                     const std::string row_id = CluTabId::Get(page.get());
                     self->ActivateRow(row_id);
                   }
                 },
                 contents->GetWeakPtr()));
}

void CluWindowState::DropRestoredCopy(content::WebContents* copy,
                                      const std::string& row_id) {
  TabStripModel* strip = browser_->GetTabStripModel();
  if (!copy || strip->GetIndexOfWebContents(copy) == TabStripModel::kNoTab) {
    return;
  }
  // The row's own page shows instead (it moves here from another window), else the empty state.
  if (strip->GetActiveWebContents() == copy && !ActivateRow(row_id)) {
    ShowEmpty();
  }
  // The strip never empties: an empty strip closes the window.
  EnsurePlaceholder();
  const int index = strip->GetIndexOfWebContents(copy);
  if (index != TabStripModel::kNoTab) {
    strip->DetachAndDeleteWebContentsAt(index);
  }
  RemovePlaceholderIfUnused();
  UpdatePageShown();
  MarkDirty();
}

void CluWindowState::NoteActivation(content::WebContents* old_contents,
                                    content::WebContents* new_contents) {
  const bool new_is_row = new_contents && !CluPlaceholder::Is(new_contents);
  CluModelService* m = model();
  if (!m) {
    if (new_is_row) {
      const std::string& id = CluTabId::Get(new_contents);
      last_active_at_.insert_or_assign(
          id, static_cast<double>(
                  base::Time::Now().InMillisecondsSinceUnixEpoch()));
      mru_.Touch(id);
      // A private window's placeholder (its opening new-tab page) goes once a page shows.
      PostToSelf(GetWeakPtr(), base::BindOnce([](CluWindowState* self) {
                   self->RemovePlaceholderIfUnused();
                 }));
    }
    return;
  }
  const int64_t now = m->NowMs();
  CluTabTree& tree = m->mutable_tree();
  // F6: the idle clock of the row being left starts now.
  if (old_contents && !CluPlaceholder::Is(old_contents) &&
      CluTabId::Has(old_contents)) {
    if (TabNode* node = tree.Mutable(CluTabId::Get(old_contents))) {
      node->last_active_at = now;
    }
  }
  if (new_is_row) {
    const std::string id = CluTabId::Get(new_contents);
    if (TabNode* node = tree.Mutable(id)) {
      node->last_active_at = now;
      mru_.Touch(id);
      if (node->section == Section::kFavorite || node->space_id == space_id_) {
        // A favorite is remembered as the active tab of the space it was opened from.
        tree.MutableSpaceState(space_id_).active_tab_id = id;
      }
    }
    PostToSelf(GetWeakPtr(), base::BindOnce([](CluWindowState* self) {
                 self->RemovePlaceholderIfUnused();
               }));
  }
  m->CommitSoon();
}

void CluWindowState::SyncRowFromPage(content::WebContents* contents,
                                     bool with_history) {
  CluModelService* m = model();
  if (!m || CluPlaceholder::Is(contents) || !CluTabId::Has(contents)) {
    return;
  }
  const std::string id = CluTabId::Get(contents);
  const TabNode* node = m->tree().Get(id);
  if (!node || !node->is_tab()) {
    return;
  }
  const std::string url = PageUrl(contents);
  const std::string title =
      CleanTitle(base::UTF16ToUTF8(contents->GetTitle()));
  const bool url_changed = IsRestorableTabUrl(url) && url != node->url;
  // An anchored row away from its pinned URL keeps the title it had there.
  const bool title_changed =
      !title.empty() && title != node->title &&
      !(node->anchored() && IsOffPinned(url, node->pinned_url));
  std::optional<std::string> nav;
  bool nav_changed = false;
  if (with_history && node->section == Section::kToday) {
    // Anchored rows restore at their pinned URL without history, so only Today rows keep it (F2).
    nav = CaptureNavState(contents);
    nav_changed = nav != node->nav_state;
  }
  if (!url_changed && !title_changed && !nav_changed) {
    return;
  }
  TabNode* row = m->mutable_tree().Mutable(id);
  if (url_changed) {
    row->url = url;
  }
  if (title_changed) {
    row->title = title;
  }
  if (nav_changed) {
    row->nav_state = std::move(nav);
  }
  m->CommitSoon();
}

void CluWindowState::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  switch (change.type()) {
    case TabStripModelChange::kInserted:
      for (const auto& inserted : change.GetInsert()->contents) {
        AdoptPage(inserted.contents);
      }
      break;
    case TabStripModelChange::kRemoved: {
      std::vector<ClosedPage> closed;
      const bool window_closing = tab_strip_model->closing_all();
      for (const auto& removed : change.GetRemove()->contents) {
        content::WebContents* contents = removed.contents;
        // The background placeholder went (an extension closed it): another takes its place once the strip settles.
        if (contents && CluPlaceholder::Is(contents) && !window_closing) {
          PostToSelf(GetWeakPtr(), base::BindOnce([](CluWindowState* self) {
                       self->RemovePlaceholderIfUnused();
                     }));
        }
        if (!contents || CluPlaceholder::Is(contents) ||
            !CluTabId::Has(contents)) {
          continue;
        }
        const std::string id = CluTabId::Get(contents);
        if (removed.remove_reason == TabRemovedReason::kInsertedIntoSidePanel) {
          continue;
        }
        // Moved to another window: the row stays with its page while that window is a Clu window. A window without
        // Clu's shell (an extension's popup) shows no rows, so a move there counts as a close once the page lands.
        if (removed.remove_reason ==
            TabRemovedReason::kInsertedIntoOtherTabStrip) {
          ClosedPage page;
          page.row_id = id;
          page.was_active = selection.old_contents == contents;
          PostToSelf(
              GetWeakPtr(),
              base::BindOnce(
                  [](base::WeakPtr<content::WebContents> moved,
                     ClosedPage page, CluWindowState* self) {
                    tabs::TabInterface* tab =
                        moved ? tabs::TabInterface::MaybeGetFromContents(
                                    moved.get())
                              : nullptr;
                    BrowserWindowInterface* to =
                        tab ? tab->GetBrowserWindowInterface() : nullptr;
                    if (!to || CluShell::From(to)) {
                      return;
                    }
                    page.capture = CluModelService::Capture(moved.get());
                    std::vector<ClosedPage> pages;
                    pages.push_back(std::move(page));
                    self->HandleClosedPages(std::move(pages));
                  },
                  contents->GetWeakPtr(), std::move(page)));
          continue;
        }
        // A page this window closed itself: the row already changed.
        if (dropping_.erase(id)) {
          continue;
        }
        CluModelService* m = model();
        if (!m || !m->tree().Get(id)) {
          continue;
        }
        // The window closes (or the app quits): rows stay, with the page's last URL and history.
        if (window_closing) {
          SyncRowFromPage(contents, /*with_history=*/true);
          continue;
        }
        ClosedPage page;
        page.row_id = id;
        page.capture = CluModelService::Capture(contents);
        page.was_active = selection.old_contents == contents;
        closed.push_back(std::move(page));
      }
      if (window_closing) {
        if (CluModelService* m = model()) {
          m->Commit();
        }
      }
      if (!closed.empty()) {
        PostToSelf(GetWeakPtr(),
                   base::BindOnce(
                       [](std::vector<ClosedPage> pages, CluWindowState* self) {
                         self->HandleClosedPages(std::move(pages));
                       },
                       std::move(closed)));
      }
      break;
    }
    case TabStripModelChange::kReplaced: {
      // A discard swaps the page's WebContents: the new one keeps the row id.
      const TabStripModelChange::Replace* replace = change.GetReplace();
      if (replace->old_contents && replace->new_contents) {
        if (CluPlaceholder::Is(replace->old_contents)) {
          CluPlaceholder::Mark(replace->new_contents);
        } else if (CluTabId::Has(replace->old_contents)) {
          const std::string id = CluTabId::Get(replace->old_contents);
          CluTabId::Set(replace->new_contents, id);
          // The new page does not carry the old one's mute; the row's mute holds.
          CluModelService* m = model();
          const TabNode* row = m ? m->tree().Get(id) : nullptr;
          SetPageMuted(replace->new_contents,
                       row ? row->muted : replace->old_contents->IsAudioMuted());
        }
      }
      break;
    }
    default:
      break;
  }
  // A closing window's last activations are no use: the rows keep the times they had.
  if (selection.active_tab_changed() && !tab_strip_model->closing_all()) {
    NoteActivation(selection.old_contents, selection.new_contents);
    Hover(std::nullopt, std::nullopt);
  }
  // A closed command bar target leaves the session without one.
  if (command_ && command_->target_tab_id &&
      !FindTab(*command_->target_tab_id)) {
    command_->target_tab_id.reset();
  }
  if (multitask_) {
    multitask_->OnTabStripModelChanged(change, selection);
  }
  UpdatePageShown();
  MarkDirty();
}

void CluWindowState::OnSplitTabChanged(const SplitTabChange& change) {
  if (multitask_) {
    multitask_->OnSplitTabChanged(change);
  }
}

void CluWindowState::OnTabChangedAt(tabs::TabInterface* tab,
                                    TabChangeType change_type) {
  if (tab && change_type == TabChangeType::kAll) {
    SyncRowFromPage(tab->GetContents(), /*with_history=*/true);
    // The tab hover card shows the page's title as it is now ("Settings" became "Settings - About Rio").
    if (hover_surface_ && hover_surface_->surface == "tab-hover" &&
        CluTabId::Has(tab->GetContents()) &&
        CluTabId::Get(tab->GetContents()) == hover_row_id_) {
      const std::string row = hover_row_id_;
      Hover(row, hover_anchor_);
    }
  }
  MarkDirty();
}

void CluWindowState::HandleClosedPages(std::vector<ClosedPage> pages) {
  CluModelService* m = model();
  if (!m) {
    return;
  }
  for (ClosedPage& page : pages) {
    // The strip already showed its neighbor; F9 picks the row that takes the closed row's place.
    std::optional<std::string> next;
    const bool shown_now_by_strip = page.was_active;
    if (shown_now_by_strip) {
      next = NextRowAfterClose(page.row_id);
    }
    m->CloseRow(page.row_id, std::move(page.capture));
    if (shown_now_by_strip) {
      LinkActivate(next);
    }
  }
}

// Pages --------------------------------------------------------------------------------------------------------------

void CluWindowState::LoadRowPage(const TabNode& row) {
  CluModelService* m = model();
  std::string url = row.url;
  std::optional<std::string> nav;
  if (row.section == Section::kToday) {
    nav = row.nav_state;
  }
  // Cmd+Shift+T after closing an anchored row brings back its last URL and history.
  if (m) {
    if (auto reopen = m->TakeReopenOverride(row.id)) {
      url = reopen->first;
      nav = reopen->second;
    }
  }
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(
          content::WebContents::CreateParams(profile()));
  const bool restored = nav && RestoreNavState(contents.get(), *nav);
  // After the history: the id goes onto every restored entry too (clu_row_stamp.h).
  CluTabId::Set(contents.get(), row.id);
  SetPageMuted(contents.get(), row.muted);
  content::WebContents* page = contents.get();
  TabStripModel* strip = browser_->GetTabStripModel();
  strip->InsertWebContentsAt(strip->count(), std::move(contents),
                             AddTabTypes::ADD_ACTIVE);
  if (restored) {
    page->GetController().LoadIfNecessary();
    return;
  }
  const GURL gurl(url);
  if (!IsRestorableTabUrl(url) || gurl.IsAboutBlank()) {
    return;
  }
  content::NavigationController::LoadURLParams params(gurl);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
  page->GetController().LoadURLWithParams(params);
}

tabs::TabInterface* CluWindowState::EnsurePlaceholder() {
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    if (CluPlaceholder::Is(strip->GetWebContentsAt(i))) {
      return strip->GetTabAtIndex(i);
    }
  }
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(
          content::WebContents::CreateParams(profile()));
  CluPlaceholder::Mark(contents.get());
  const int index = strip->InsertWebContentsAt(
      strip->count(), std::move(contents), AddTabTypes::ADD_NONE);
  return strip->GetTabAtIndex(index);
}

void CluWindowState::RemovePlaceholderIfUnused() {
  if (!ActiveTab()) {
    return;
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  bool kept = false;
  for (int i = 0; i < strip->count();) {
    // A row shows, so no placeholder here is the active tab.
    if (!CluPlaceholder::Is(strip->GetWebContentsAt(i)) || !kept) {
      kept = kept || CluPlaceholder::Is(strip->GetWebContentsAt(i));
      ++i;
      continue;
    }
    strip->DetachAndDeleteWebContentsAt(i);
  }
  if (!kept) {
    EnsurePlaceholder();
  }
}

void CluWindowState::ShowEmpty() {
  ActivateTab(EnsurePlaceholder());
  UpdatePageShown();
  MarkDirty();
}

void CluWindowState::UpdatePageShown() {
  // Onboarding is the frame WebUI's full-window layer: the page view would sit on top of it.
  CluAppSettings* settings = CluAppSettings::Get();
  const bool onboarding = !profile()->IsOffTheRecord() && settings &&
                          settings->onboarding_show();
  shell_->SetPageShown(!onboarding &&
                       (!rows_ready() || ActiveTab() != nullptr));
}

std::optional<std::string> CluWindowState::NextRowAfter(
    const std::string& row_id) {
  CluModelService* m = model();
  if (!m) {
    return std::nullopt;
  }
  auto usable = [&](const std::string& id) {
    return id != row_id && m->IsInSpace(id, space_id_) &&
           !m->pending_clear().contains(id) && !m->WindowShowing(id, this);
  };
  for (const std::string& id : mru_.list()) {
    if (usable(id)) {
      return id;
    }
  }
  for (const std::string& id : m->tree().Today(space_id_)) {
    if (usable(id)) {
      return id;
    }
  }
  for (const std::string& id :
       m->tree().SidebarOrder(space_id_, std::string(), m->pending_clear())) {
    if (usable(id)) {
      return id;
    }
  }
  return std::nullopt;
}

std::optional<std::string> CluWindowState::NextRowAfterClose(
    const std::string& row_id) {
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(row_id) : nullptr;
  if (!node) {
    return std::nullopt;
  }
  RemovedRow removed;
  removed.node = *node;
  removed.siblings = m->tree().ListOf(*node);
  return m->NextActiveFor(this, removed, {row_id});
}

bool CluWindowState::TakePageFrom(content::WebContents* contents) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  BrowserWindowInterface* source_browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  CluWindowState* source =
      source_browser ? CluWindowState::GetOrCreate(source_browser) : nullptr;
  if (!source || source == this) {
    return false;
  }
  const std::string id = CluTabId::Get(contents);
  // A page lives in one window at a time: the window that had it falls back to its most recent row (02-tabs Model).
  if (source->ActiveRowId() == id) {
    source->LinkActivate(source->NextRowAfter(id));
  }
  TabStripModel* source_strip = source_browser->GetTabStripModel();
  if (source_strip->count() == 1) {
    source->EnsurePlaceholder();
  }
  const int index = source_strip->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab) {
    return false;
  }
  std::unique_ptr<tabs::TabModel> detached =
      source_strip->DetachTabAtForInsertion(index);
  TabStripModel* strip = browser_->GetTabStripModel();
  strip->InsertDetachedTabAt(strip->count(), std::move(detached),
                             AddTabTypes::ADD_ACTIVE);
  return true;
}

// Rows ---------------------------------------------------------------------------------------------------------------

bool CluWindowState::ActivateRow(const std::string& row_id) {
  CluModelService* m = model();
  if (!m) {
    tabs::TabInterface* tab = FindTab(row_id);
    if (tab) {
      ActivateTab(tab);
    }
    return tab != nullptr;
  }
  const TabNode* node = m->tree().Get(row_id);
  if (!node || !node->is_tab()) {
    return false;
  }
  // A row of another space takes the window to that space, as a switch does (last space, the toast).
  if (!node->space_id.empty() && node->space_id != space_id_ &&
      m->FindSpace(node->space_id)) {
    EnterSpace(node->space_id);
  }
  if (tabs::TabInterface* tab = FindTab(row_id)) {
    ActivateTab(tab);
  } else if (content::WebContents* contents = m->FindContents(row_id)) {
    if (!TakePageFrom(contents)) {
      return false;
    }
  } else {
    LoadRowPage(*node);
  }
  // A split's row brings its other pane along (docs/chromium/m6.md).
  multitask_->ShowSplitOf(row_id);
  UpdatePageShown();
  MarkDirty();
  return true;
}

base::expected<std::string, std::string> CluWindowState::CreateRow(
    const GURL& typed_url,
    const std::optional<std::string>& space_id,
    bool activate) {
  // A rio:// alias opens its chrome:// page (M8).
  const GURL url = ResolveCluUrl(typed_url);
  if (!IsTypedUrlAllowed(url)) {
    return base::unexpected("blocked-url");
  }
  CluModelService* m = model();
  if (!m) {
    // A private window: a plain tab, as in M2.
    if (space_id && *space_id != space_id_) {
      return base::unexpected("not-found");
    }
    NavigateParams params(browser_, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = activate ? WindowOpenDisposition::kNewForegroundTab
                                  : WindowOpenDisposition::kNewBackgroundTab;
    Navigate(&params);
    content::WebContents* contents = params.navigated_or_inserted_contents;
    if (!contents) {
      return base::unexpected("navigation-failed");
    }
    return base::ok(CluTabId::Get(contents));
  }
  const std::string space = space_id.value_or(space_id_);
  if (!m->FindSpace(space)) {
    return base::unexpected("not-found");
  }
  NewTab init;
  init.url = url.spec();
  base::expected<std::string, TreeError> id = m->mutable_tree().InsertTab(
      init, DropTarget::Today(space, 0), m->NowMs());
  if (!id.has_value()) {
    return base::unexpected(std::string(TreeErrorReason(id.error())));
  }
  m->Commit();
  if (activate) {
    ActivateRow(id.value());
  }
  return base::ok(id.value());
}

void CluWindowState::CloseRowCommand(const std::optional<std::string>& row_id) {
  CluModelService* m = model();
  if (!m) {
    tabs::TabInterface* tab = row_id ? FindTab(*row_id) : ActiveTab();
    if (!tab) {
      if (!row_id) {
        browser_->GetWindow()->Close();
      }
      return;
    }
    if (const std::optional<int> index = IndexOf(tab)) {
      browser_->GetTabStripModel()->CloseWebContentsAt(
          *index, TabCloseTypes::CLOSE_USER_GESTURE |
                      TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    }
    return;
  }
  const std::string id = row_id.value_or(ActiveRowId());
  if (id.empty()) {
    // F9.2: no active tab closes the window.
    if (!row_id) {
      browser_->GetWindow()->Close();
    }
    return;
  }
  const TabNode* node = m->tree().Get(id);
  if (!node || !node->is_tab()) {
    return;
  }
  tabs::TabInterface* tab = FindTab(id);
  if (!tab) {
    // No page here: a row without a page, or one another window shows, follows F9 at once.
    m->CloseRow(id, std::nullopt);
    return;
  }
  // F9: the next row shows first, so the strip's own pick never flashes; then the page closes through the strip,
  // where a "Leave site?" prompt may keep it. The row changes once the page is gone (OnTabStripModelChanged).
  // A pinned or favorite row only unloads: nothing is selected after it, the card shows the empty page and the pill
  // its placeholder.
  if (ActiveRowId() == id) {
    if (node->anchored()) {
      ShowEmpty();
    } else if (const std::optional<std::string> next = NextRowAfterClose(id)) {
      ActivateRow(*next);
    } else {
      ShowEmpty();
    }
  }
  if (const std::optional<int> index = IndexOf(FindTab(id))) {
    browser_->GetTabStripModel()->CloseWebContentsAt(
        *index, TabCloseTypes::CLOSE_USER_GESTURE |
                    TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }
}

std::optional<std::string> CluWindowState::DuplicateRow(
    const std::string& row_id) {
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(row_id) : nullptr;
  if (!node || !node->is_tab()) {
    return std::nullopt;
  }
  // F10: below a Today source, else at the top of the window's space; URL, title, mute and history come along.
  const bool from_today = node->section == Section::kToday;
  const std::string space = from_today ? node->space_id : space_id_;
  NewTab init;
  init.title = node->title;
  init.muted = node->muted;
  if (content::WebContents* contents = m->FindContents(row_id)) {
    init.url = PageUrl(contents);
    init.title = base::UTF16ToUTF8(contents->GetTitle());
    init.nav_state = CaptureNavState(contents);
  } else {
    init.url = node->url;
    init.nav_state = node->nav_state;
  }
  if (!IsRestorableTabUrl(init.url)) {
    init.url = node->url;
  }
  const int index = from_today ? node->position + 1 : 0;
  base::expected<std::string, TreeError> id = m->mutable_tree().InsertTab(
      init, DropTarget::Today(space, index), m->NowMs());
  if (!id.has_value()) {
    return std::nullopt;
  }
  m->Commit();
  ActivateRow(id.value());
  return id.value();
}

void CluWindowState::SetRowMuted(const std::string& row_id, bool muted) {
  CluModelService* m = model();
  if (!m) {
    if (tabs::TabInterface* tab = FindTab(row_id)) {
      SetPageMuted(tab->GetContents(), muted);
      MarkDirty();
    }
    return;
  }
  TabNode* node = m->mutable_tree().Mutable(row_id);
  if (!node || !node->is_tab()) {
    return;
  }
  // F11: the row keeps it, so every new page of the row starts muted.
  node->muted = muted;
  if (content::WebContents* contents = m->FindContents(row_id)) {
    SetPageMuted(contents, muted);
  }
  m->Commit();
}

base::expected<void, TreeError> CluWindowState::MovePrivatePages(
    const std::vector<std::string>& ids,
    const DropTarget& target) {
  if (target.section != Section::kToday) {
    return base::unexpected(TreeError::kBadTarget);
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  std::vector<tabs::TabInterface*> moved;
  for (const std::string& id : ids) {
    tabs::TabInterface* tab = FindTab(id);
    if (!tab) {
      return base::unexpected(TreeError::kNotFound);
    }
    // A split's pages stay side by side in the strip.
    if (tab->IsSplit()) {
      return base::unexpected(TreeError::kBadTarget);
    }
    if (!std::ranges::contains(moved, tab)) {
      moved.push_back(tab);
    }
  }
  if (moved.empty()) {
    return base::unexpected(TreeError::kNotFound);
  }
  // The window's list is its pages in strip order (TakeSnapshot()), here without the moved ones.
  std::vector<tabs::TabInterface*> rest;
  for (int i = 0; i < strip->count(); ++i) {
    tabs::TabInterface* tab = strip->GetTabAtIndex(i);
    if (!CluPlaceholder::Is(tab->GetContents()) &&
        !std::ranges::contains(moved, tab)) {
      rest.push_back(tab);
    }
  }
  if (target.index < 0 || static_cast<size_t>(target.index) > rest.size()) {
    return base::unexpected(TreeError::kBadTarget);
  }
  tabs::TabInterface* before = static_cast<size_t>(target.index) < rest.size()
                                   ? rest[static_cast<size_t>(target.index)]
                                   : nullptr;
  if (before && before->IsSplit()) {
    return base::unexpected(TreeError::kBadTarget);
  }
  // Each lands right before `before`, or at the end, so the moved pages keep the given order.
  for (tabs::TabInterface* tab : moved) {
    const int from = strip->GetIndexOfTab(tab);
    int to = strip->count() - 1;
    if (before) {
      const int at = strip->GetIndexOfTab(before);
      to = from < at ? at - 1 : at;
    }
    if (from != to) {
      strip->MoveWebContentsAt(from, to, /*select_after_move=*/false);
    }
  }
  MarkDirty();
  return {};
}

namespace {

// The app settings AI tab's Tidy Tab Titles switch (docs/chromium/parity-settings.md section 1): on unless stored as
// false, and on where there are no app settings (clu_unittests).
bool TidyTitlesOn() {
  CluAppSettings* settings = CluAppSettings::Get();
  return !settings || settings->Global().FindBool("ai.tidyTitles").value_or(true);
}

}  // namespace

base::expected<void, TreeError> CluWindowState::MoveRows(
    const std::vector<std::string>& ids,
    const DropTarget& target,
    bool toast) {
  CluModelService* m = model();
  if (!m || ids.empty()) {
    return base::unexpected(TreeError::kNotFound);
  }
  const std::string active = ActiveRowId();
  CluTabTree& tree = m->mutable_tree();
  // Tidy Tab Titles (docs/chromium/parity-command.md): Today tabs pinned without a rename get a tidier title.
  std::vector<std::string> pinning;
  if (target.section == Section::kPinned && TidyTitlesOn()) {
    for (const std::string& id : ids) {
      const TabNode* node = tree.Get(id);
      if (node && node->is_tab() && node->section == Section::kToday &&
          node->title_override.value_or(std::string()).empty()) {
        pinning.push_back(id);
      }
    }
  }
  base::expected<void, TreeError> result =
      ids.size() == 1 ? tree.Move(ids[0], target, m->NowMs())
                      : tree.MoveMany(ids, target, m->NowMs());
  if (!result.has_value()) {
    if (toast) {
      ShowToast(std::string(TreeErrorText(result.error())), std::nullopt,
                base::Milliseconds(1500));
    }
    return result;
  }
  // The on-device model names the tab about a second later; its rules answer when the Mac has no model.
  for (const std::string& id : pinning) {
    const TabNode* node = tree.Get(id);
    if (!node) {
      continue;
    }
    AskOnDeviceModel(
        {"title", node->title, node->url},
        base::BindOnce(&CluWindowState::ApplyTidyTitle, GetWeakPtr(), id,
                       node->title, node->url));
  }
  m->Commit();
  // F8: the window stays in its space; if its row left the space, the next row shows.
  if (!active.empty() && !m->IsInSpace(active, space_id_)) {
    LinkActivate(NextRowAfter(active));
  }
  return result;
}

void CluWindowState::ApplyTidyTitle(const std::string& row_id,
                                    const std::string& title,
                                    const std::string& url,
                                    std::optional<std::string> answer) {
  CluModelService* m = model();
  TabNode* node = m ? m->mutable_tree().Mutable(row_id) : nullptr;
  // Renamed meanwhile, or no longer pinned: the user's choice stands.
  if (!node || node->section != Section::kPinned ||
      !node->title_override.value_or(std::string()).empty()) {
    return;
  }
  std::optional<std::string> tidy =
      answer ? CleanModelTitle(*answer, kMaxTidyTitle) : std::nullopt;
  if (!tidy) {
    tidy = TidyPinnedTitle(title, GURL(url));
  }
  if (!tidy || *tidy == title) {
    return;
  }
  node->title_override = std::move(*tidy);
  m->Commit();
}

// static
void CluWindowState::OnLinkHover(content::WebContents* source,
                                 const GURL& url) {
  tabs::TabInterface* tab = tabs::TabInterface::MaybeGetFromContents(source);
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  CluWindowState* window = browser ? CluWindowState::GetOrCreate(browser) : nullptr;
  if (window) {
    window->LinkHovered(url);
  }
}

// Hover previews (ai.hoverPreviews): resting on a link with Shift held for a moment opens it in Peek, over the page.
// Shift may go down before or after the pointer reaches the link, so the key is read while a link is under the pointer.
void CluWindowState::LinkHovered(const GURL& url) {
  hover_link_timer_.Stop();
  hover_link_url_ = GURL();
  hover_shift_since_ = base::TimeTicks();
  CluAppSettings* settings = CluAppSettings::Get();
  const bool on = !settings || settings->Global()
                                   .FindBool("ai.hoverPreviews")
                                   .value_or(true);
  if (!on || is_private() || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  hover_link_url_ = url;
  if (ShiftKeyDown()) {
    hover_shift_since_ = base::TimeTicks::Now();
  }
  hover_link_timer_.Start(
      FROM_HERE, kHoverShiftPoll,
      base::BindRepeating(
          [](base::WeakPtr<CluWindowState> window) {
            if (!window) {
              return;
            }
            if (!ShiftKeyDown()) {
              window->hover_shift_since_ = base::TimeTicks();
              return;
            }
            const base::TimeTicks now = base::TimeTicks::Now();
            if (window->hover_shift_since_.is_null()) {
              window->hover_shift_since_ = now;
            }
            if (now - window->hover_shift_since_ < kHoverPreviewDelay) {
              return;
            }
            window->hover_link_timer_.Stop();
            const GURL url = std::move(window->hover_link_url_);
            window->hover_link_url_ = GURL();
            if (!window->ActiveRowId().empty()) {
              CluPeek& peek = window->multitask().peek();
              peek.Open(window->ActiveRowId(), url, content::Referrer());
              peek.Summarize();
              window->WatchHoverPreview(
                  display::Screen::Get()->GetCursorScreenPoint());
            }
          },
          GetWeakPtr()));
}

// The Peek covers the page, so the page no longer reports the pointer leaving its link. The cursor is read
// instead: once it moves past the link's reach from where the preview opened, a preview the hover opened closes.
// A Peek the user clicked open stays.
void CluWindowState::WatchHoverPreview(const gfx::Point& origin) {
  hover_preview_timer_.Start(
      FROM_HERE, kHoverShiftPoll,
      base::BindRepeating(
          [](base::WeakPtr<CluWindowState> window, gfx::Point origin) {
            if (window && window->CloseHoverPreviewIfLeft(
                              origin,
                              display::Screen::Get()->GetCursorScreenPoint())) {
              window->hover_preview_timer_.Stop();
            }
          },
          GetWeakPtr(), origin));
}

bool CluWindowState::CloseHoverPreviewIfLeft(const gfx::Point& origin,
                                             const gfx::Point& cursor) {
  if (!multitask_ || !multitask_->has_peek() ||
      !multitask_->peek().from_hover()) {
    return true;
  }
  const gfx::Vector2d moved = cursor - origin;
  if (std::abs(moved.x()) <= kHoverLinkReach &&
      std::abs(moved.y()) <= kHoverLinkReach) {
    return false;
  }
  multitask_->peek().Close();
  return true;
}

base::expected<void, TreeError> CluWindowState::TogglePin(
    const std::string& row_id) {
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(row_id) : nullptr;
  if (!node || !node->is_tab()) {
    return base::unexpected(TreeError::kNotFound);
  }
  const std::string space = node->space_id.empty() ? space_id_ : node->space_id;
  const DropTarget target =
      node->section == Section::kPinned
          ? DropTarget::Today(space, 0)
          : DropTarget::Pinned(space, std::string(),
                               static_cast<int>(m->tree().Pinned(space).size()));
  return MoveRows({row_id}, target, /*toast=*/true);
}

bool CluWindowState::ResetToPinned(const std::string& row_id) {
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(row_id) : nullptr;
  if (!node || !node->is_tab() || !node->pinned_url) {
    return false;
  }
  const std::string pinned = *node->pinned_url;
  if (content::WebContents* contents = m->FindContents(row_id)) {
    content::NavigationController::LoadURLParams params((GURL(pinned)));
    params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
    contents->GetController().LoadURLWithParams(params);
    return true;
  }
  m->mutable_tree().Mutable(row_id)->url = pinned;
  m->Commit();
  return true;
}

bool CluWindowState::MoveRowToNewWindow(const std::string& row_id) {
  CluModelService* m = model();
  const TabNode* node = m ? m->tree().Get(row_id) : nullptr;
  if (!node || !node->is_tab()) {
    return false;
  }
  // The page (or a fresh one for a row without a page) goes into a new window through Chromium's navigator, which
  // opens the window; that window's state finds the row by the page's Clu tab id.
  std::unique_ptr<content::WebContents> contents;
  if (FindTab(row_id)) {
    if (ActiveRowId() == row_id) {
      LinkActivate(NextRowAfter(row_id));
    }
    TabStripModel* strip = browser_->GetTabStripModel();
    if (strip->count() == 1) {
      EnsurePlaceholder();
    }
    // Activating the next row or the placeholder may change the strip; look the page up again.
    const std::optional<int> index = IndexOf(FindTab(row_id));
    if (!index) {
      return false;
    }
    contents = strip->DetachWebContentsAtForInsertion(*index);
  } else if (m->FindContents(row_id)) {
    // Another window shows it: it moves from there when the new window activates it.
    return false;
  } else {
    contents = content::WebContents::Create(
        content::WebContents::CreateParams(profile()));
    CluTabId::Set(contents.get(), row_id);
    SetPageMuted(contents.get(), node->muted);
    content::NavigationController::LoadURLParams params((GURL(node->url)));
    params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
    contents->GetController().LoadURLWithParams(params);
  }
  NavigateParams params(browser_, std::move(contents));
  params.disposition = WindowOpenDisposition::kNewWindow;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
  return true;
}

int CluWindowState::ClearToday(const std::optional<std::string>& space_id) {
  CluModelService* m = model();
  return m ? m->ClearToday(space_id.value_or(space_id_), this) : 0;
}

void CluWindowState::ReopenClosed() {
  if (CluModelService* m = model()) {
    m->ReopenClosed(this, base::DoNothing());
  }
}

std::vector<std::string> CluWindowState::RowOrder() {
  if (CluModelService* m = model()) {
    return m->tree().SidebarOrder(space_id_, ActiveRowId(), m->pending_clear());
  }
  // A private window's rows are its pages, in strip order.
  std::vector<std::string> order;
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    if (!CluPlaceholder::Is(strip->GetWebContentsAt(i))) {
      order.push_back(CluTabId::Get(strip->GetWebContentsAt(i)));
    }
  }
  return order;
}

bool CluWindowState::GotoRow(int number) {
  if (number < 1) {
    return false;
  }
  const std::vector<std::string> order = RowOrder();
  // F12: past the last row nothing happens (Cmd+9 is the 9th row).
  if (static_cast<size_t>(number) > order.size()) {
    return false;
  }
  return ActivateRow(order[number - 1]);
}

bool CluWindowState::StepRow(int direction) {
  const std::string active = ActiveRowId();
  const std::vector<std::string> order = RowOrder();
  if (order.empty()) {
    return false;
  }
  const auto it = std::ranges::find(order, active);
  if (it == order.end()) {
    return ActivateRow(direction > 0 ? order.front() : order.back());
  }
  const int index = static_cast<int>(it - order.begin()) + direction;
  // No wrap (F12).
  if (index < 0 || index >= static_cast<int>(order.size())) {
    return false;
  }
  return ActivateRow(order[index]);
}

void CluWindowState::SetArchivePanel(bool open) {
  if (archive_panel_ == open) {
    return;
  }
  VLOG(1) << "clu-peek archive panel open=" << open << " peek_open="
          << (sidebar_peek_ && sidebar_peek_->is_open());
  archive_panel_ = open;
  Hover(std::nullopt, std::nullopt);
  // Closing in the floating sidebar, the hold ends before the panel narrows and the pointer is checked after.
  if (!open && sidebar_peek_) {
    sidebar_peek_->LibraryClosing();
  }
  // The Library takes 417 pt: the page card gives way while it is open.
  shell_->SetLibraryOpen(open);
  // A width its Spaces view asked for lasts while it is open; it asks again when it shows.
  if (!open) {
    shell_->SetLibraryWidth(0);
    if (sidebar_peek_) {
      sidebar_peek_->LibraryClosed();
    }
  }
  // With the sidebar hidden the Library shows only in the floating sidebar, so opening it there (Cmd+Shift+L, the
  // Window menu, the command bar) opens the floating sidebar too. Where that cannot open, the Library closes again
  // rather than wait unseen for the next edge hover.
  if (open && shell_->layout().hot_zone && sidebar_peek_ &&
      !sidebar_peek_->is_open()) {
    sidebar_peek_->SetOpen(true);
    if (!sidebar_peek_->is_open()) {
      SetArchivePanel(false);
      return;
    }
  }
  MarkDirty();
}

void CluWindowState::OpenMenu(
    const std::string& menu,
    const std::optional<std::string>& node_id,
    const std::vector<std::string>& tab_ids,
    const gfx::Rect& anchor,
    base::OnceCallback<void(std::optional<std::string>)> done) {
  Hover(std::nullopt, std::nullopt);
  tab_menu_->Open(menu, node_id, tab_ids, anchor, std::move(done));
}

// CluModelService::WindowLink ----------------------------------------------------------------------------------------

const std::string& CluWindowState::LinkWindowId() {
  return window_id_;
}

std::string CluWindowState::LinkSpaceId() {
  return space_id_;
}

std::string CluWindowState::LinkActiveNodeId() {
  return ActiveRowId();
}

std::vector<std::string> CluWindowState::LinkMru() {
  return mru_.list();
}

content::WebContents* CluWindowState::LinkContents(std::string_view node_id) {
  tabs::TabInterface* tab = FindTab(node_id);
  return tab ? tab->GetContents() : nullptr;
}

void CluWindowState::LinkEachContents(
    base::FunctionRef<void(std::string_view, content::WebContents*)> each) {
  // The pages FindTab finds, in its order.
  TabStripModel* strip = browser_->GetTabStripModel();
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!CluPlaceholder::Is(contents) && CluTabId::Has(contents)) {
      each(CluTabId::Get(contents), contents);
    }
  }
}

void CluWindowState::LinkActivate(const std::optional<std::string>& node_id) {
  if (!node_id || !ActivateRow(*node_id)) {
    ShowEmpty();
  }
}

void CluWindowState::LinkDropPage(const std::string& node_id) {
  tabs::TabInterface* tab = FindTab(node_id);
  if (!tab) {
    return;
  }
  TabStripModel* strip = browser_->GetTabStripModel();
  // The strip never empties: an empty strip closes the window.
  if (strip->count() == 1) {
    ShowEmpty();
  }
  const std::optional<int> index = IndexOf(FindTab(node_id));
  if (!index) {
    return;
  }
  // No "Leave site?" wait: the row already went to the archive or back to its pinned URL (02-tabs F6, F9).
  dropping_.insert(node_id);
  strip->DetachAndDeleteWebContentsAt(*index);
}

void CluWindowState::LinkRowsRemoved(const std::vector<std::string>& ids) {
  for (const std::string& id : ids) {
    mru_.Remove(id);
    switcher_->TabRemoved(id);
    if (id == hover_row_id_) {
      Hover(std::nullopt, std::nullopt);
    }
  }
  UpdateEventMonitor();
  MarkDirty();
}

void CluWindowState::LinkToast(const std::string& text,
                               const std::optional<std::string>& undo_command) {
  ShowToast(text, undo_command,
            undo_command ? CluModelService::kClearUndo
                         : base::Milliseconds(1500));
}

void CluWindowState::LinkDropToasts(const std::string& command) {
  if (std::erase_if(toasts_, [&](const ToastSnapshot& toast) {
        return toast.action_command == command;
      })) {
    UpdateToastSurface();
  }
}

}  // namespace clu

namespace clu {

bool CluWindowState::OpenInternalPage(const GURL& url) {
  CluModelService* m = model();
  if (!m || !url.is_valid()) {
    return false;
  }
  // A row of the same page (scheme and host) in this space: the one already shown first, then the first in sidebar
  // order (02-tabs F12), loaded or not.
  auto same_page = [&](const std::string& spec) {
    const GURL row(spec);
    return row.SchemeIs(url.scheme()) && row.host() == url.host();
  };
  std::string found;
  const std::string active = ActiveRowId();
  if (const TabNode* node = active.empty() ? nullptr : m->tree().Get(active);
      node && same_page(node->url)) {
    found = active;
  }
  if (found.empty()) {
    for (const std::string& id :
         m->tree().SidebarOrder(space_id_, active, m->pending_clear())) {
      const TabNode* node = m->tree().Get(id);
      if (node && same_page(node->url)) {
        found = id;
        break;
      }
    }
  }
  if (found.empty()) {
    return CreateRow(url, space_id_, /*activate=*/true).has_value();
  }
  if (!ActivateRow(found)) {
    return false;
  }
  // A page of the settings (rio://settings/privacy), a section or a request in the query (Chromium's password
  // import) the row does not show yet: the row goes there.
  if (tabs::TabInterface* tab = FindTab(found);
      tab && (url.has_ref() || url.has_query() || url.path() != "/") &&
      tab->GetContents()->GetLastCommittedURL() != url) {
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_AUTO_BOOKMARK;
    tab->GetContents()->GetController().LoadURLWithParams(params);
  }
  FocusActivePage();
  return true;
}

std::unique_ptr<ui::MenuModel> CluWindowState::NavHistoryModel(bool back) {
  return std::make_unique<BackForwardMenuModel>(
      browser_, back ? BackForwardMenuModel::ModelType::kBackward
                     : BackForwardMenuModel::ModelType::kForward);
}

std::unique_ptr<ui::MenuModel> CluWindowState::ToolbarMenuModel() {
  return std::make_unique<CluToolbarMenuModel>(GetWeakPtr());
}

void CluWindowState::OpenToolbarMenu(const gfx::Point& anchor) {
  views::WebView* frame = shell_->frame_view();
  if (!frame || !frame->GetWidget() || shell_->is_mini()) {
    return;
  }
  nav_menu_runner_.reset();
  nav_menu_model_ = ToolbarMenuModel();
  gfx::Rect bounds(anchor, gfx::Size());
  views::View::ConvertRectToScreen(frame, &bounds);
  nav_menu_runner_ = std::make_unique<views::MenuRunner>(
      nav_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  nav_menu_runner_->RunMenuAt(frame->GetWidget(), /*button_controller=*/nullptr,
                              bounds, views::MenuAnchorPosition::kTopLeft,
                              ui::mojom::MenuSourceType::kMouse);
}

void CluWindowState::OpenNavHistory(bool back, const gfx::Rect& anchor) {
  views::WebView* frame = shell_->frame_view();
  if (!frame || !frame->GetWidget() || !ActiveTab()) {
    return;
  }
  nav_menu_runner_.reset();
  nav_menu_model_ = NavHistoryModel(back);
  gfx::Rect bounds = anchor;
  views::View::ConvertRectToScreen(frame, &bounds);
  nav_menu_runner_ = std::make_unique<views::MenuRunner>(
      nav_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  nav_menu_runner_->RunMenuAt(frame->GetWidget(), /*button_controller=*/nullptr,
                              bounds, views::MenuAnchorPosition::kTopLeft,
                              ui::mojom::MenuSourceType::kMouse);
}

}  // namespace clu
