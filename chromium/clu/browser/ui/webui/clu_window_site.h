// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WINDOW_SITE_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WINDOW_SITE_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "clu/browser/site/clu_site_service.h"
#include "clu/browser/ui/webui/clu_site_state.h"
#include "components/prefs/pref_change_registrar.h"
#include "url/gurl.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}  // namespace content

// One window's share of M7 (docs/chromium/m7.md): the site, blocker and Site tweaks fields of its tabs, its action row
// and its profile's extensions in state, the Site controls popover, the extensions menu and the tweak editor in its
// top overlay, the command table's blocking and tweaks commands, and the anchor its extension popups hang from.
namespace clu {

class CluSitePage;
class CluWindowState;
struct WindowSnapshot;

class CluWindowSite : public CluSiteService::Observer,
                      public ToolbarActionsModel::Observer {
 public:
  explicit CluWindowSite(CluWindowState& window);
  CluWindowSite(const CluWindowSite&) = delete;
  CluWindowSite& operator=(const CluWindowSite&) = delete;
  ~CluWindowSite() override;

  // Adds the M7 fields to the window's snapshot.
  void Fill(WindowSnapshot& s);

  // blocking.turnOff, blocking.turnOn, tweaks.open, tweaks.zap and tweaks.toggle on the page the window shows.
  // nullopt for any other id; false when the command cannot run now (no site, a private window).
  std::optional<bool> DispatchCommand(std::string_view id, bool run);

  // ---- the page a command acts on ---------------------------------------------------------------------------------

  // The live page of row `tab_id` in this window, or null.
  content::WebContents* ContentsFor(const std::string& tab_id);
  // The site of row `tab_id`: its page's document, or the row's URL without a page. nullopt for an unknown row or a
  // URL with no site.
  std::optional<std::string> SiteOf(const std::string& tab_id);
  // The profile's site data, or null (a profile Clu does not serve). In a private window it is the regular profile's,
  // read only.
  CluSiteService* service();
  // Writes from a private window are refused, so nothing about a private visit is stored.
  bool can_write();
  // The window's Clu profile id as state carries it (ProfileState.id).
  std::string profile_id();
  // True when row `tab_id` is in this window: a page of its strip or a row of its profile's model.
  bool HasRow(const std::string& tab_id);
  // Closes every prompt of the top overlay (zap mode needs the page clickable).
  void ClosePrompts();
  // Developer Mode of `site`: the user's choice, else on for local sites. SetDevMode stores the choice, and
  // every window publishes it.
  static bool IsDevMode(const std::string& site);
  static void SetDevMode(const std::string& site, bool on);

  // ---- surfaces ---------------------------------------------------------------------------------------------------

  // siteControls.open: the popover under the shield at `anchor` (frame coordinates).
  void OpenSiteControls(const gfx::Rect& anchor);
  // extensions.openMenu: the extensions menu under its button.
  void OpenExtensionsMenu(const gfx::Rect& anchor);
  // tweaks.openEditor: the editor docked right over the page card, for row `tab_id` (shown first when it is not the
  // window's front row). The IpcError reason on failure: "not-found", "no-site", "private-window".
  base::expected<void, std::string> OpenTweakEditor(const std::string& tab_id);

  // extensions.runAction: the popup of extension `id` hangs from `anchor`. False for an unknown extension.
  bool RunAction(const std::string& id, const gfx::Rect& anchor);

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;
  void OnToolbarActionsModelShutdown() override;

  // CluSiteService::Observer:
  void OnSiteDataLoaded() override;
  void OnBlockingChanged(const std::string& site) override;
  void OnTweakChanged(const std::string& site) override;

 private:
  // A popover or panel in the top overlay; `done` runs when it closes, however it closes.
  std::string OpenSurface(const std::string& surface,
                          const gfx::Rect& rect,
                          const std::string& modality,
                          base::DictValue props,
                          base::OnceClosure done);
  // The editor belongs to one row and one site: it closes when the row leaves the front or moves to another site.
  void CheckEditor();
  void OnEditorClosed(const std::string& tab_id);
  void MarkDirty();
  // The front page's Developer Mode strip, applied after the snapshot (the layout change publishes again).
  void ApplyDevStrip(bool shown);
  // Something about the extensions changed: the row and the list are built again on the next publish.
  void MarkActionsDirty();

  raw_ref<CluWindowState> window_;
  std::string editor_instance_id_;
  std::string editor_tab_id_;
  std::string editor_site_;
  // The action row and the extension list, built again only when an action, a pin, an icon or the page changes:
  // icons are PNG-encoded, and blocked counts publish many times per page load.
  bool actions_dirty_ = true;
  std::optional<ActionRowSnapshot> action_row_;
  std::vector<ExtensionInfoSnapshot> extensions_;
  raw_ptr<content::WebContents> actions_contents_ = nullptr;
  GURL actions_url_;
  std::map<std::string, base::CallbackListSubscription> icon_subscriptions_;
  std::vector<base::CallbackListSubscription> subscriptions_;
  PrefChangeRegistrar dev_mode_prefs_;
  base::ScopedObservation<CluSiteService, CluSiteService::Observer>
      site_observation_{this};
  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      actions_observation_{this};
  base::WeakPtrFactory<CluWindowSite> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WINDOW_SITE_H_
