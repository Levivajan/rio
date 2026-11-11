// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_BLOCKER_H_
#define CLU_BROWSER_SITE_CLU_BLOCKER_H_

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "clu/browser/site/clu_blocker_rules.h"
#include "clu/browser/site/clu_site_service.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"

class GURL;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace extensions {
class Extension;
}  // namespace extensions

namespace clu {

// The built-in ad and tracker blocker of one profile (docs/plan/08-extensions.md F5 to F8, docs/chromium/m7.md), on
// by default. It loads Clu's component extension, gives it the compiled filter lists as
// declarativeNetRequest dynamic rules (reinstalled only when the lists change) and the profile's per-site exceptions
// as one session rule, so Chromium's own request pipeline blocks in every frame of every tab, private windows
// included. Pages ask it for the hiding CSS of their host; tabs read their blocked count from the component
// extension's action, which Chromium keeps per tab and resets per page.
//
// It is ready once the rules and exceptions are in place; the navigation throttle (clu_site_hooks.cc) holds every
// page until then, so the first page after launch is already blocked.
class CluBlocker : public extensions::ExtensionRegistryObserver,
                   public extensions::ExtensionActionDispatcher::Observer,
                   public CluSiteService::Observer {
 public:
  CluBlocker(Profile* profile, CluSiteService* site);
  CluBlocker(const CluBlocker&) = delete;
  CluBlocker& operator=(const CluBlocker&) = delete;
  ~CluBlocker() override;

  // Tests: what the blocker loads in place of the overlay pak's IDR_CLU_OVERLAY_PAGE_BLOCKER_{RULES,COSMETIC}_JSON
  // (the fixture under chromium/clu/test/data/blocker). Set before the profile's first page.
  static void SetResourcesForTesting(std::string rules_json,
                                     std::string cosmetic_json);

  bool ready() const { return ready_; }
  // Runs `callback` once the blocker is ready (at once when it is).
  void WhenReady(base::OnceClosure callback);

  // The requests Chromium blocked on the page `contents` shows (0 for a page that is not a tab).
  int BlockedCount(content::WebContents* contents) const;
  // The hiding CSS for a page at `url`: empty for a site with blocking off, a URL with no site or before the lists
  // are read.
  std::string CosmeticCss(const GURL& url) const;
  // The version of the rules in place, empty before.
  const std::string& rules_version() const { return rules_version_; }

  base::WeakPtr<CluBlocker> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  // Runs `callback` with the page whose blocked count changed.
  base::CallbackListSubscription AddCountCallback(
      base::RepeatingCallback<void(content::WebContents*)> callback);

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnShutdown(extensions::ExtensionRegistry* registry) override;

  // extensions::ExtensionActionDispatcher::Observer:
  void OnExtensionActionUpdated(
      extensions::ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) override;
  void OnShuttingDown() override;

  // CluSiteService::Observer:
  void OnBlockingChanged(const std::string& site) override;

 private:
  struct Resources;

  void Start();
  void OnResourcesParsed(std::unique_ptr<Resources> resources);
  const extensions::Extension* GetExtension() const;
  // Once the component extension is loaded, its lists read and the profile's rows read: the dynamic rules when the
  // lists changed, then the exceptions.
  void MaybeInstall();
  void OnRulesInstalled(std::string version, std::optional<std::string> error);
  void ApplyExceptions();
  void OnExceptionsApplied(std::optional<std::string> error);
  void MarkReady();

  raw_ptr<Profile> profile_;
  raw_ptr<CluSiteService> site_;
  bool extension_loaded_ = false;
  bool resources_parsed_ = false;
  bool installing_ = false;
  bool installed_ = false;
  bool ready_ = false;
  std::optional<BlockerRules> rules_;
  std::optional<CosmeticData> cosmetic_;
  std::string rules_version_;
  // An exceptions update in flight, and another one asked for meanwhile.
  bool applying_ = false;
  bool reapply_ = false;
  std::set<std::string> reload_after_apply_;
  std::vector<base::OnceClosure> on_ready_;
  base::RepeatingCallbackList<void(content::WebContents*)> count_callbacks_;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::ScopedObservation<extensions::ExtensionActionDispatcher,
                          extensions::ExtensionActionDispatcher::Observer>
      action_observation_{this};
  base::WeakPtrFactory<CluBlocker> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_BLOCKER_H_
