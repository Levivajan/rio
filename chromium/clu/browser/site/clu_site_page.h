// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_PAGE_H_
#define CLU_BROWSER_SITE_CLU_SITE_PAGE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "clu/browser/site/clu_tweak_rules.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "extensions/browser/script_executor.h"

namespace content {
class BrowserContext;
class NavigationHandle;
}  // namespace content

// What Clu puts into one page (docs/plan/08-extensions.md F6, F9 to F11, docs/chromium/m7.md): the blocker's hiding
// CSS as a user-origin sheet and the site's tweak CSS as an injected author sheet (neither is subject to page CSP),
// the tweak's JS once per document in the component extension's isolated world, live CSS previews, and zap mode. It
// injects through Chromium's extension ScriptExecutor on behalf of Clu's component extension, into the main frame of
// every page of a profile Clu serves: tabs of every window, popups, Mini windows and Peek.
namespace clu {

class CluSitePage : public content::WebContentsObserver,
                    public content::WebContentsUserData<CluSitePage> {
 public:
  ~CluSitePage() override;

  // Made for `contents` when its profile is one Clu serves; null otherwise.
  static CluSitePage* GetOrCreate(content::WebContents* contents);
  static CluSitePage* Get(content::WebContents* contents);

  // Pages of the regular profile `context` (or its private windows) whose main frame shows `site`.
  static std::vector<CluSitePage*> PagesOnSite(content::BrowserContext* context,
                                               std::string_view site);
  // The tweak of `site` changed: its pages show the new CSS, zap bars the new count.
  static void OnTweakChanged(content::BrowserContext* context,
                             std::string_view site);
  // Advanced's "Enable Site tweaks on websites you visit" (tweaks.enabled) changed: every page of `context` shows or
  // drops its saved CSS at once, and a page whose tweak has JS reloads, so the JS runs or is gone.
  static void OnTweaksSwitched(content::BrowserContext* context);
  // The switch's value; on without Local State.
  static bool TweaksEnabled();
  // Reloads the pages of `site` (blocking turned on or off; a tweak's JS changed).
  static void ReloadSite(content::BrowserContext* context,
                         std::string_view site);
  // Runs `callback` with a page whose site, zap mode or preview changed, so windows republish.
  static base::CallbackListSubscription AddChangedCallback(
      base::RepeatingCallback<void(content::WebContents*)> callback);

  // Tests: the zap picker script in place of the overlay pak's IDR_CLU_OVERLAY_PAGE_ZAP_JS.
  static void SetZapScriptForTesting(std::string script);

  // The site of the document the main frame shows, or nullopt.
  const std::optional<std::string>& site() const { return site_; }

  // Live previews of the tweak editor (tweaks.previewCss, tweaks.previewControls): this page only, nothing stored.
  void PreviewCss(std::string css);
  void PreviewControls(const TweakControls& controls);
  // The editor closed: the page shows the saved tweak again.
  void EndPreview();
  // Shows the saved tweak (after a save, a toggle or a zap) without the drafts.
  void ClearDrafts();

  // Zap mode (F11): one pick at a time, each answer checked and stored, until Done, Esc, a navigation or the page
  // hiding. False when the page shows no site.
  bool StartZap();
  void StopZap();
  // Cmd+Z or the banner's Undo: the last zap of the site comes back.
  void UndoZap();
  bool zap_active() const { return zap_active_; }
  const std::optional<std::string>& zap_last_result() const {
    return zap_last_result_;
  }

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void RenderFrameDeleted(content::RenderFrameHost* frame) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<CluSitePage>;

  // `author`: the tweak's own CSS goes in as author origin, so it competes with the site's rules as page CSS does;
  // the hiding sheet is user origin, whose !important rules beat every page rule.
  struct Sheet {
    std::string code;
    std::string key;
    bool author = false;
  };

  explicit CluSitePage(content::WebContents* contents);

  // Re-inserts the tweak CSS when it changed: the old sheet goes, then the new one.
  void ApplyTweakCss();
  void InsertSheet(std::string css, std::optional<Sheet>& slot, bool author);
  void RemoveSheet(std::optional<Sheet>& slot);
  void RunJs(std::string code,
             bool want_result,
             extensions::ScriptExecutor::ScriptFinishedCallback callback);
  void RunTweakJs();

  void PickNext();
  void OnPick(int generation,
              std::vector<extensions::ScriptExecutor::FrameResult> results);
  void HandleZap(const base::DictValue& pick);
  void SetZapResult(std::optional<std::string> message);
  void UpdateZapBar();
  // Ends zap mode; with `in_page`, the picker leaves the page too.
  void EndZap(bool in_page);
  void NotifyChanged();

  extensions::ScriptExecutor executor_;
  std::optional<std::string> site_;
  std::optional<Sheet> cosmetic_sheet_;
  std::optional<Sheet> tweak_sheet_;
  // The sheets of documents in the back/forward cache, by their main frame: a document restored from it still has
  // them (and its tweak JS ran), so they are reconciled, not inserted again.
  struct CachedSheets {
    std::optional<Sheet> cosmetic;
    std::optional<Sheet> tweak;
  };
  std::map<content::GlobalRenderFrameHostId, CachedSheets> cached_sheets_;
  std::optional<std::string> draft_css_;
  std::optional<TweakControls> draft_controls_;
  bool zap_active_ = false;
  // Bumped whenever zap mode starts or ends, so a pick from an older session is ignored.
  int zap_generation_ = 0;
  std::optional<std::string> zap_last_result_;
  base::WeakPtrFactory<CluSitePage> weak_ptr_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_PAGE_H_
