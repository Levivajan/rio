// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_blocker.h"

#include <algorithm>
#include <utility>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/one_shot_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "clu/browser/site/clu_site_extension.h"
#include "clu/browser/site/clu_site_key.h"
#include "clu/browser/site/clu_site_page.h"
#include "clu/grit/clu_overlay_resources.h"
#include "components/sessions/content/session_tab_helper.h"
#include "extensions/browser/api/declarative_net_request/prefs_helper.h"
#include "extensions/browser/api/declarative_net_request/rules_monitor_service.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/api/declarative_net_request.h"
#include "extensions/common/extension.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace clu {

namespace dnr = extensions::declarative_net_request;
namespace dnr_api = extensions::api::declarative_net_request;

namespace {

struct TestResources {
  std::string rules;
  std::string cosmetic;
};

std::optional<TestResources>& TestResourcesSlot() {
  static base::NoDestructor<std::optional<TestResources>> slot;
  return *slot;
}

std::vector<dnr_api::Rule> ToRules(const base::ListValue& list,
                                   int* skipped) {
  std::vector<dnr_api::Rule> rules;
  rules.reserve(list.size());
  for (const base::Value& value : list) {
    auto rule = dnr_api::Rule::FromValue(value);
    if (!rule.has_value()) {
      ++*skipped;
      continue;
    }
    rules.push_back(std::move(rule).value());
  }
  return rules;
}

}  // namespace

struct CluBlocker::Resources {
  std::optional<BlockerRules> rules;
  std::optional<CosmeticData> cosmetic;
};

// static
void CluBlocker::SetResourcesForTesting(std::string rules_json,
                                        std::string cosmetic_json) {
  TestResourcesSlot() =
      TestResources{std::move(rules_json), std::move(cosmetic_json)};
}

CluBlocker::CluBlocker(Profile* profile, CluSiteService* site)
    : profile_(profile), site_(site) {
  site_->AddObserver(this);
  registry_observation_.Observe(extensions::ExtensionRegistry::Get(profile_));
  action_observation_.Observe(
      extensions::ExtensionActionDispatcher::Get(profile_));

  // The lists are megabytes of JSON: read them off the UI thread.
  std::string rules_json;
  std::string cosmetic_json;
  if (const std::optional<TestResources>& test = TestResourcesSlot()) {
    rules_json = test->rules;
    cosmetic_json = test->cosmetic;
  } else {
    ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
    rules_json =
        bundle.LoadDataResourceString(IDR_CLU_OVERLAY_PAGE_BLOCKER_RULES_JSON);
    cosmetic_json = bundle.LoadDataResourceString(
        IDR_CLU_OVERLAY_PAGE_BLOCKER_COSMETIC_JSON);
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING},
      base::BindOnce(
          [](std::string rules_json, std::string cosmetic_json) {
            auto resources = std::make_unique<Resources>();
            resources->rules = ParseBlockerRules(rules_json);
            resources->cosmetic = ParseCosmeticData(cosmetic_json);
            return resources;
          },
          std::move(rules_json), std::move(cosmetic_json)),
      base::BindOnce(&CluBlocker::OnResourcesParsed,
                     weak_ptr_factory_.GetWeakPtr()));

  extensions::ExtensionSystem* system =
      extensions::ExtensionSystem::Get(profile_);
  if (system->is_ready()) {
    // Posted: the blocker is still being made, and loading the extension calls every registry observer, which may ask
    // for this profile's blocker.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&CluBlocker::Start, weak_ptr_factory_.GetWeakPtr()));
  } else {
    system->ready().Post(FROM_HERE, base::BindOnce(&CluBlocker::Start,
                                                   weak_ptr_factory_.GetWeakPtr()));
  }
  site_->WhenLoaded(base::BindOnce(&CluBlocker::MaybeInstall,
                                   weak_ptr_factory_.GetWeakPtr()));
}

CluBlocker::~CluBlocker() {
  site_->RemoveObserver(this);
}

void CluBlocker::WhenReady(base::OnceClosure callback) {
  if (ready_) {
    std::move(callback).Run();
    return;
  }
  on_ready_.push_back(std::move(callback));
}

void CluBlocker::Start() {
  if (GetExtension()) {
    extension_loaded_ = true;
    MaybeInstall();
    return;
  }
  // Loads at once (the extension system is ready); OnExtensionLoaded() follows.
  extensions::ComponentLoader* loader =
      extensions::ComponentLoader::Get(profile_);
  if (!loader ||
      loader
          ->Add(kCluExtensionManifest,
                base::FilePath::FromASCII(kCluExtensionRoot))
          .empty()) {
    // Without the extension nothing blocks, but pages must still load.
    LOG(ERROR) << "clu: the blocker's component extension did not load";
    MarkReady();
  }
}

const extensions::Extension* CluBlocker::GetExtension() const {
  return extensions::ExtensionRegistry::Get(profile_)
      ->enabled_extensions()
      .GetByID(kCluExtensionId);
}

void CluBlocker::OnExtensionLoaded(content::BrowserContext* browser_context,
                                   const extensions::Extension* extension) {
  if (extension->id() != kCluExtensionId) {
    return;
  }
  extension_loaded_ = true;
  // Chromium keeps the per-tab count of blocked requests on the action only when the extension asks for it as its
  // badge (declarativeNetRequest.setExtensionActionOptions).
  dnr::PrefsHelper helper(*extensions::ExtensionPrefs::Get(profile_));
  if (!helper.GetUseActionCountAsBadgeText(kCluExtensionId)) {
    helper.SetUseActionCountAsBadgeText(kCluExtensionId, true);
  }
  if (dnr::RulesMonitorService* rules =
          dnr::RulesMonitorService::Get(profile_)) {
    rules->action_tracker().OnActionCountAsBadgeTextPreferenceEnabled(
        kCluExtensionId);
  }
  // A reload (an update of the extension system) drops the session rules.
  installed_ = false;
  MaybeInstall();
}

void CluBlocker::OnResourcesParsed(std::unique_ptr<Resources> resources) {
  resources_parsed_ = true;
  rules_ = std::move(resources->rules);
  cosmetic_ = std::move(resources->cosmetic);
  if (!rules_) {
    LOG(ERROR) << "clu: the blocker's rules are unreadable; nothing is blocked";
  }
  if (!cosmetic_) {
    LOG(ERROR) << "clu: the blocker's hiding rules are unreadable";
  }
  MaybeInstall();
}

void CluBlocker::MaybeInstall() {
  if (!extension_loaded_ || !resources_parsed_ || !site_->loaded() ||
      installing_ || installed_) {
    return;
  }
  const extensions::Extension* extension = GetExtension();
  dnr::RulesMonitorService* service = dnr::RulesMonitorService::Get(profile_);
  if (!extension || !service) {
    MarkReady();
    return;
  }
  if (!rules_ || rules_->version == site_->blocker_version()) {
    // The rules the extension holds are current (or there are none to give): only the exceptions.
    installed_ = true;
    rules_version_ = rules_ ? rules_->version : site_->blocker_version();
    ApplyExceptions();
    return;
  }
  installing_ = true;
  int skipped = rules_->skipped;
  std::vector<dnr_api::Rule> rules = ToRules(rules_->rules, &skipped);
  if (skipped) {
    LOG(WARNING) << "clu: blocker skipped " << skipped << " unreadable rules";
  }
  // Every id a dynamic rule can have: the old lists go whatever their size.
  std::vector<int> remove;
  remove.reserve(kMaxBlockerRules);
  for (int id = 1; id <= kMaxBlockerRules; ++id) {
    remove.push_back(id);
  }
  // Queued by the service until the extension's stored rules are loaded.
  service->UpdateDynamicRules(
      *extension, std::move(remove), std::move(rules),
      base::BindOnce(&CluBlocker::OnRulesInstalled,
                     weak_ptr_factory_.GetWeakPtr(), rules_->version));
}

void CluBlocker::OnRulesInstalled(std::string version,
                                  std::optional<std::string> error) {
  installing_ = false;
  installed_ = true;
  if (error) {
    // The old rules stay; the next launch tries again.
    LOG(ERROR) << "clu: blocker rules not installed: " << *error;
    rules_version_ = site_->blocker_version();
  } else {
    rules_version_ = version;
    site_->SetBlockerVersion(version);
  }
  ApplyExceptions();
}

void CluBlocker::ApplyExceptions() {
  const extensions::Extension* extension = GetExtension();
  dnr::RulesMonitorService* service = dnr::RulesMonitorService::Get(profile_);
  if (!extension || !service) {
    MarkReady();
    return;
  }
  if (applying_) {
    reapply_ = true;
    return;
  }
  applying_ = true;
  int skipped = 0;
  std::vector<dnr_api::Rule> rules =
      ToRules(ExceptionSessionRules(site_->exceptions()), &skipped);
  if (skipped) {
    LOG(WARNING) << "clu: blocker skipped " << skipped << " exception rules";
  }
  // Every id an exception rule can have: the old set goes whatever its size.
  std::vector<int> remove;
  remove.reserve(kMaxExceptionRules);
  for (int id = kExceptionRuleId; id < kExceptionRuleId + kMaxExceptionRules;
       ++id) {
    remove.push_back(id);
  }
  service->UpdateSessionRules(
      *extension, std::move(remove), std::move(rules),
      base::BindOnce(&CluBlocker::OnExceptionsApplied,
                     weak_ptr_factory_.GetWeakPtr()));
}

void CluBlocker::OnExceptionsApplied(std::optional<std::string> error) {
  applying_ = false;
  if (error) {
    LOG(ERROR) << "clu: blocker exceptions not applied: " << *error;
  }
  if (reapply_) {
    reapply_ = false;
    ApplyExceptions();
    return;
  }
  // The pages of a site whose switch flipped reload now that the rule is in place, so the change shows at once
  // (08-extensions "Toggle").
  std::set<std::string> sites = std::move(reload_after_apply_);
  reload_after_apply_.clear();
  for (const std::string& site : sites) {
    CluSitePage::ReloadSite(profile_, site);
  }
  MarkReady();
}

void CluBlocker::MarkReady() {
  if (ready_) {
    return;
  }
  ready_ = true;
  std::vector<base::OnceClosure> callbacks = std::move(on_ready_);
  for (base::OnceClosure& callback : callbacks) {
    std::move(callback).Run();
  }
}

int CluBlocker::BlockedCount(content::WebContents* contents) const {
  const extensions::Extension* extension = GetExtension();
  if (!contents || !extension) {
    return 0;
  }
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
  if (!tab_id.is_valid()) {
    return 0;
  }
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(profile_)->GetExtensionAction(
          *extension);
  // GetDNRActionCount() is -1 until the tab's first count arrives (it would show "-1 blocked").
  return action ? std::max(0, action->GetDNRActionCount(tab_id.id())) : 0;
}

std::string CluBlocker::CosmeticCss(const GURL& url) const {
  const std::optional<std::string> site = SiteKey(url);
  if (!cosmetic_ || !site || !site_->IsBlockingEnabled(*site)) {
    return std::string();
  }
  return CosmeticCssFor(*cosmetic_, url.host());
}

base::CallbackListSubscription CluBlocker::AddCountCallback(
    base::RepeatingCallback<void(content::WebContents*)> callback) {
  return count_callbacks_.Add(std::move(callback));
}

void CluBlocker::OnShutdown(extensions::ExtensionRegistry* registry) {
  registry_observation_.Reset();
}

void CluBlocker::OnExtensionActionUpdated(
    extensions::ExtensionAction* extension_action,
    content::WebContents* web_contents,
    content::BrowserContext* browser_context) {
  if (extension_action->extension_id() == kCluExtensionId && web_contents) {
    count_callbacks_.Notify(web_contents);
  }
}

void CluBlocker::OnShuttingDown() {
  action_observation_.Reset();
}

void CluBlocker::OnBlockingChanged(const std::string& site) {
  reload_after_apply_.insert(site);
  if (installed_) {
    ApplyExceptions();
  }
}

}  // namespace clu
