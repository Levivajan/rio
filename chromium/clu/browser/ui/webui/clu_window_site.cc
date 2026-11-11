// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_window_site.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "clu/browser/site/clu_blocker.h"
#include "clu/browser/site/clu_site_extension.h"
#include "clu/browser/site/clu_site_key.h"
#include "clu/browser/site/clu_site_page.h"
#include "clu/browser/site/clu_site_profile.h"
#include "clu/browser/site/clu_tweak_rules.h"
#include "clu/browser/ui/webui/clu_extension_actions.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr char kSiteControlsSurface[] = "site-controls";
constexpr char kExtensionsMenuSurface[] = "extensions-menu";
constexpr char kTweakEditorSurface[] = "tweak-editor";
// The editor docks right over the page card.
constexpr int kEditorWidth = 380;

gfx::Rect EditorRect(const gfx::Rect& page) {
  const int width = std::min(kEditorWidth, std::max(0, page.width()));
  return gfx::Rect(page.right() - width, page.y(), width,
                   std::max(0, page.height()));
}

std::optional<TweakSummarySnapshot> Summary(const SiteTweak* tweak) {
  if (!tweak) {
    return std::nullopt;
  }
  auto blank = [](const std::string& s) {
    return base::TrimWhitespaceASCII(s, base::TRIM_ALL).empty();
  };
  return TweakSummarySnapshot{.enabled = tweak->enabled,
                              .has_css = !blank(tweak->css),
                              .has_js = !blank(tweak->js),
                              .has_controls = tweak->controls.has_value(),
                              .zap_count = static_cast<int>(tweak->zaps.size())};
}

}  // namespace

CluWindowSite::CluWindowSite(CluWindowState& window) : window_(window) {
  Profile* profile = window_->profile();
  if (CluSiteService* site = GetCluSiteService(profile)) {
    site_observation_.Observe(site);
  }
  if (ToolbarActionsModel* model = ToolbarActionsModel::Get(profile)) {
    actions_observation_.Observe(model);
  }
  auto dirty = base::BindRepeating(
      [](base::WeakPtr<CluWindowSite> self, content::WebContents*) {
        if (self) {
          self->MarkDirty();
        }
      },
      weak_ptr_factory_.GetWeakPtr());
  if (CluBlocker* blocker = GetCluBlocker(profile)) {
    subscriptions_.push_back(blocker->AddCountCallback(dirty));
  }
  subscriptions_.push_back(CluSitePage::AddChangedCallback(dirty));
  if (PrefService* local_state = g_browser_process->local_state()) {
    dev_mode_prefs_.Init(local_state);
    dev_mode_prefs_.Add(kCluDevModeSitesPref,
                        base::BindRepeating(&CluWindowSite::MarkDirty,
                                            base::Unretained(this)));
  }
}

// static
bool CluWindowSite::IsDevMode(const std::string& site) {
  PrefService* local_state = g_browser_process->local_state();
  const std::optional<bool> chosen =
      local_state ? local_state->GetDict(kCluDevModeSitesPref).FindBool(site)
                  : std::nullopt;
  return chosen.value_or(DevModeDefault(site));
}

// static
void CluWindowSite::SetDevMode(const std::string& site, bool on) {
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state || !IsSiteKey(site)) {
    return;
  }
  ScopedDictPrefUpdate update(local_state, kCluDevModeSitesPref);
  // The default needs no row, so the dictionary holds only real choices.
  if (on == DevModeDefault(site)) {
    update->Remove(site);
  } else {
    update->Set(site, on);
  }
}

void CluWindowSite::ApplyDevStrip(bool shown) {
  window_->shell().SetDevStrip(shown);
}

CluWindowSite::~CluWindowSite() = default;

void CluWindowSite::MarkDirty() {
  window_->MarkDirty();
}

void CluWindowSite::MarkActionsDirty() {
  actions_dirty_ = true;
  window_->MarkDirty();
}

CluSiteService* CluWindowSite::service() {
  return GetCluSiteService(window_->profile());
}

bool CluWindowSite::can_write() {
  return !window_->is_private();
}

std::string CluWindowSite::profile_id() {
  CluWindowState& w = *window_;
  return w.model_ ? w.model_->profile_id() : w.profile_id_;
}

bool CluWindowSite::HasRow(const std::string& tab_id) {
  return window_->FindTab(tab_id) ||
         (window_->model() && window_->model()->tree().Get(tab_id));
}

void CluWindowSite::ClosePrompts() {
  std::vector<std::string> ids;
  for (const OverlaySurfaceSnapshot& prompt : window_->prompts_) {
    ids.push_back(prompt.instance_id);
  }
  for (const std::string& id : ids) {
    window_->CloseOverlayInstance(id);
  }
}

content::WebContents* CluWindowSite::ContentsFor(const std::string& tab_id) {
  tabs::TabInterface* tab = window_->FindTab(tab_id);
  return tab ? tab->GetContents() : nullptr;
}

std::optional<std::string> CluWindowSite::SiteOf(const std::string& tab_id) {
  if (content::WebContents* contents = ContentsFor(tab_id)) {
    if (CluSitePage* page = CluSitePage::Get(contents)) {
      return page->site();
    }
    return SiteKey(contents->GetLastCommittedURL());
  }
  if (CluModelService* model = window_->model()) {
    if (const TabNode* node = model->tree().Get(tab_id)) {
      return SiteKey(GURL(node->url));
    }
  }
  return std::nullopt;
}

void CluWindowSite::Fill(WindowSnapshot& s) {
  Profile* profile = window_->profile();
  CluSiteService* site = service();
  CluBlocker* blocker = GetCluBlocker(profile);
  for (TabSnapshot& tab : s.tabs) {
    content::WebContents* contents = tab.live ? ContentsFor(tab.id) : nullptr;
    CluSitePage* page = CluSitePage::Get(contents);
    const std::optional<std::string> key =
        page ? page->site() : SiteKey(GURL(tab.url));
    tab.site.site = key;
    if (key && site) {
      tab.site.blocking_enabled = site->IsBlockingEnabled(*key);
      tab.site.dev_mode = IsDevMode(*key);
      tab.site.tweak = Summary(site->Tweak(*key));
    }
    if (contents && blocker && tab.site.blocking_enabled) {
      tab.site.blocked_count = blocker->BlockedCount(contents);
    }
    if (page && page->zap_active()) {
      tab.site.zap_active = true;
      tab.site.zap_last_result = page->zap_last_result();
    }
  }
  tabs::TabInterface* active_tab = window_->ActiveTab();
  content::WebContents* active =
      active_tab ? active_tab->GetContents() : nullptr;
  const GURL active_url = active ? active->GetLastCommittedURL() : GURL();
  // The strip follows the front page's site; a Mini window has no band.
  const std::optional<std::string> active_site = SiteKey(active_url);
  const bool strip = !window_->shell().is_mini() && active_site &&
                     IsDevMode(*active_site);
  if (strip != window_->shell().chrome_state().dev_strip) {
    // Not while the snapshot is taken: the layout change publishes again.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CluWindowSite::ApplyDevStrip,
                                  weak_ptr_factory_.GetWeakPtr(), strip));
  }
  if (actions_dirty_ || active != actions_contents_ ||
      active_url != actions_url_) {
    actions_dirty_ = false;
    actions_contents_ = active;
    actions_url_ = active_url;
    action_row_ =
        BuildActionRow(window_->browser(), profile, s.profile_id, active);
    extensions_ = BuildExtensionInfos(profile);
    // Icons load after the first build; each change builds the row again.
    std::map<std::string, base::CallbackListSubscription> icons;
    if (action_row_) {
      for (const ActionButtonSnapshot& action : action_row_->actions) {
        auto it = icon_subscriptions_.find(action.id);
        icons[action.id] =
            it != icon_subscriptions_.end()
                ? std::move(it->second)
                : ObserveActionIcon(
                      window_->browser(), action.id,
                      base::BindRepeating(&CluWindowSite::MarkActionsDirty,
                                          weak_ptr_factory_.GetWeakPtr()));
      }
    }
    icon_subscriptions_ = std::move(icons);
  }
  s.action_row = action_row_;
  if (action_row_) {
    s.action_row->profile_id = s.profile_id;
  }
  s.extensions = extensions_;
  if (!editor_instance_id_.empty()) {
    // Not while the snapshot is taken: closing changes the window's surfaces.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CluWindowSite::CheckEditor,
                                  weak_ptr_factory_.GetWeakPtr()));
  }
}

std::optional<bool> CluWindowSite::DispatchCommand(std::string_view id,
                                                   bool run) {
  const bool blocking = id == "blocking.turnOff" || id == "blocking.turnOn";
  const bool tweaks =
      id == "tweaks.open" || id == "tweaks.zap" || id == "tweaks.toggle";
  if (!blocking && !tweaks) {
    return std::nullopt;
  }
  const std::string active = window_->ActiveRowId();
  CluSiteService* site_service = service();
  const std::optional<std::string> site =
      active.empty() ? std::nullopt : SiteOf(active);
  if (!site || !site_service || !site_service->loaded() || !can_write()) {
    return false;
  }
  if (blocking) {
    const bool enable = id == "blocking.turnOn";
    if (site_service->IsBlockingEnabled(*site) == enable) {
      return false;
    }
    if (run) {
      site_service->SetBlockingEnabled(*site, enable);
    }
    return true;
  }
  if (id == "tweaks.open") {
    if (run) {
      std::ignore = OpenTweakEditor(active);
    }
    return true;
  }
  if (id == "tweaks.zap") {
    CluSitePage* page = CluSitePage::Get(ContentsFor(active));
    if (!page || !page->site()) {
      return false;
    }
    if (run) {
      page->StartZap();
    }
    return true;
  }
  // tweaks.toggle: only a tweak with something in it has a switch.
  const SiteTweak* tweak = site_service->Tweak(*site);
  if (!tweak || IsEmptyTweak(*tweak)) {
    return false;
  }
  if (run) {
    const bool had_js = !base::TrimWhitespaceASCII(tweak->js, base::TRIM_ALL)
                             .empty();
    if (site_service->SetTweakEnabled(*site, !tweak->enabled) && had_js) {
      CluSitePage::ReloadSite(window_->profile(), *site);
    }
  }
  return true;
}

std::string CluWindowSite::OpenSurface(const std::string& surface,
                                       const gfx::Rect& rect,
                                       const std::string& modality,
                                       base::DictValue props,
                                       base::OnceClosure done) {
  CluWindowState& w = *window_;
  OverlaySurfaceSnapshot prompt;
  prompt.instance_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  prompt.surface = surface;
  prompt.modality = modality;
  prompt.rect = rect;
  prompt.props = std::move(props);
  const std::string instance_id = prompt.instance_id;
  // One prompt at a time: whatever was open closes (and hears so) first.
  w.FailPrompts();
  w.prompts_.clear();
  w.prompts_.push_back(std::move(prompt));
  w.prompt_callbacks_[instance_id] = base::BindOnce(
      [](base::OnceClosure done, base::Value) { std::move(done).Run(); },
      std::move(done));
  w.Hover(std::nullopt, std::nullopt);
  w.MarkDirty();
  w.Flush();
  w.UpdateTopOverlay();
  return instance_id;
}

void CluWindowSite::OpenSiteControls(const gfx::Rect& anchor) {
  OpenSurface(kSiteControlsSurface, anchor, "light-dismiss", base::DictValue(),
              base::DoNothing());
}

void CluWindowSite::OpenExtensionsMenu(const gfx::Rect& anchor) {
  OpenSurface(kExtensionsMenuSurface, anchor, "light-dismiss",
              base::DictValue(), base::DoNothing());
}

base::expected<void, std::string> CluWindowSite::OpenTweakEditor(
    const std::string& tab_id) {
  if (!can_write()) {
    return base::unexpected("private-window");
  }
  if (!HasRow(tab_id)) {
    return base::unexpected("not-found");
  }
  std::optional<std::string> site = SiteOf(tab_id);
  if (!site) {
    return base::unexpected("no-site");
  }
  // A row menu can name a background row; the editor edits the page on screen.
  if (window_->ActiveRowId() != tab_id) {
    window_->ActivateRow(tab_id);
  }
  const std::string instance_id = OpenSurface(
      kTweakEditorSurface, EditorRect(window_->shell().layout().page_rect),
      "passive",
      base::DictValue().Set("tabId", tab_id).Set("site", *site),
      base::BindOnce(&CluWindowSite::OnEditorClosed,
                     weak_ptr_factory_.GetWeakPtr(), tab_id));
  editor_instance_id_ = instance_id;
  editor_tab_id_ = tab_id;
  editor_site_ = *site;
  return base::ok();
}

void CluWindowSite::OnEditorClosed(const std::string& tab_id) {
  if (editor_tab_id_ == tab_id) {
    editor_instance_id_.clear();
    editor_tab_id_.clear();
    editor_site_.clear();
  }
  // Closing without saving shows the saved CSS again.
  if (CluSitePage* page = CluSitePage::Get(ContentsFor(tab_id))) {
    page->EndPreview();
  }
}

void CluWindowSite::CheckEditor() {
  if (editor_instance_id_.empty()) {
    return;
  }
  const std::optional<std::string> site = SiteOf(editor_tab_id_);
  if (window_->ActiveRowId() == editor_tab_id_ && site &&
      *site == editor_site_) {
    return;
  }
  // A Save now would write this site's draft onto another one.
  const std::string instance_id = editor_instance_id_;
  window_->CloseOverlayInstance(instance_id);
}

bool CluWindowSite::RunAction(const std::string& id, const gfx::Rect& anchor) {
  if (!IsListedExtension(window_->profile(), id)) {
    return false;
  }
  window_->shell().SetExtensionAnchorRect(id, anchor);
  return RunExtensionAction(window_->browser(), id);
}

void CluWindowSite::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  MarkActionsDirty();
}

void CluWindowSite::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  MarkActionsDirty();
}

void CluWindowSite::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  // The blocker's own action changes with every blocked request; its count reaches state through the blocker.
  if (id != kCluExtensionId) {
    MarkActionsDirty();
  }
}

void CluWindowSite::OnToolbarModelInitialized() {
  MarkActionsDirty();
}

void CluWindowSite::OnToolbarPinnedActionsChanged() {
  MarkActionsDirty();
}

void CluWindowSite::OnToolbarActionsModelShutdown() {
  actions_observation_.Reset();
}

void CluWindowSite::OnSiteDataLoaded() {
  MarkDirty();
}

void CluWindowSite::OnBlockingChanged(const std::string& site) {
  MarkDirty();
}

void CluWindowSite::OnTweakChanged(const std::string& site) {
  MarkDirty();
}

}  // namespace clu
