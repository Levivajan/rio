// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_model_service_factory.h"

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/chrome_serialized_navigation_driver.h"
#include "chrome/browser/signin/chrome_signin_pref_names.h"
#include "chrome/common/chrome_paths.h"
#include "clu/browser/importer/clu_site_storage.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_row_stamp.h"
#include "clu/browser/model/clu_store.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/theme/clu_overlay_scrollbars.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_store.h"
#include "components/safe_browsing/core/common/safebrowsing_switches.h"
#include "components/translate/core/browser/translate_pref_names.h"

namespace clu {

// static
CluModelService* CluModelServiceFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  return static_cast<CluModelService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
CluModelService* CluModelService::Get(Profile* profile) {
  return CluModelServiceFactory::GetForProfile(profile);
}

// static
CluModelServiceFactory* CluModelServiceFactory::GetInstance() {
  static base::NoDestructor<CluModelServiceFactory> instance;
  return instance.get();
}

CluModelServiceFactory::CluModelServiceFactory()
    : ProfileKeyedServiceFactory("CluModelService",
                                 ProfileSelections::BuildForRegularProfile()) {
  // Built before any session or tab restore runs. Chromium installs this same driver as the content one later in
  // startup (ChromeBrowserMainParts), so the handler lands where every restore looks.
  RegisterRowStampHandler(ChromeSerializedNavigationDriver::GetInstance());
}

CluModelServiceFactory::~CluModelServiceFactory() = default;

std::unique_ptr<KeyedService>
CluModelServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  auto model = std::make_unique<CluModelService>(
      profile->GetPath()
          .AppendASCII(kCluStoreDirName)
          .AppendASCII(kCluStoreFileName),
      /*clock=*/nullptr, /*tick_clock=*/nullptr);
  // The first callback once the rows are read, before any window's: the model learns its Clu id and its spaces
  // (docs/chromium/m4.md). The service outlives every model, and the model owns the callback.
  if (CluSpacesService* spaces = CluSpacesService::Get()) {
    model->WhenLoaded(base::BindOnce(&CluSpacesService::AttachModel,
                                     base::Unretained(spaces),
                                     profile->GetPath(),
                                     base::Unretained(model.get())));
  }
  return model;
}

void CluModelServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  const base::Value* registered = nullptr;
  // Google's translate servers refuse a fork's key, so Rio never offers to translate a page. A profile that
  // turned it on in chrome://settings/languages keeps its choice.
  if (registry->defaults()->GetValue(translate::prefs::kOfferTranslateEnabled,
                                     &registered)) {
    registry->SetDefaultPrefValue(translate::prefs::kOfferTranslateEnabled,
                                  base::Value(false));
  }
  // A test can build a profile without Chromium's sign-in prefs.
  if (!registry->defaults()->GetValue(
          prefs::kHistoryPageHistorySyncPromoShownCountPerProfile,
          &registered)) {
    return;
  }
  registry->SetDefaultPrefValue(
      prefs::kHistoryPageHistorySyncPromoShownCountPerProfile,
      base::Value(kHistorySyncPromoShownByDefault));
}

void RequestEarlySafeBrowsingListUpdate() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (!command_line->HasSwitch(
          safe_browsing::switches::kSbFastInitialListsUpdate)) {
    command_line->AppendSwitch(
        safe_browsing::switches::kSbFastInitialListsUpdate);
  }
}

void EnsureCluServiceFactoriesBuilt() {
  CluModelServiceFactory::GetInstance();
  // Site storage an import staged goes into its profiles now, before any profile and its storage
  // open. Once per process; blocking is still allowed this early in startup.
  static bool merged = false;
  base::FilePath user_data;
  if (!merged && base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    merged = true;
    MergePendingSiteStorage(user_data);
  }
  // Before any profile loads, so Safe Browsing's list updater, made when the first profile starts the service,
  // already sees it.
  RequestEarlySafeBrowsingListUpdate();
  // Before any window or page: every renderer starts with overlay scrollbars.
  InstallOverlayScrollbars();
}

}  // namespace clu
