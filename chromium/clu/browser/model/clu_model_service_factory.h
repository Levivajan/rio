// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_MODEL_SERVICE_FACTORY_H_
#define CLU_BROWSER_MODEL_CLU_MODEL_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

namespace clu {

class CluModelService;

// One CluModelService per regular profile. Off-the-record and guest profiles get none, so private tabs keep no rows.
// Built in EnsureBrowserContextKeyedServiceFactoriesBuilt() (patch
// chrome-browser-profiles-chrome_browser_main_extra_parts_profiles.cc.patch), as every keyed service factory must be.
class CluModelServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static CluModelService* GetForProfile(Profile* profile);
  static CluModelServiceFactory* GetInstance();

  CluModelServiceFactory(const CluModelServiceFactory&) = delete;
  CluModelServiceFactory& operator=(const CluModelServiceFactory&) = delete;

 private:
  friend base::NoDestructor<CluModelServiceFactory>;

  CluModelServiceFactory();
  ~CluModelServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  // Runs after Chromium registered every profile pref, so it can change their defaults: chrome://history's "Sync
  // history" card never shows, since Rio has no account to sync to (docs/chromium/fixes.md).
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
};

// The default of prefs::kHistoryPageHistorySyncPromoShownCountPerProfile in Rio: the history page counts its sync promo
// as shown this often already, past Chromium's limit of 5 showings, so it never shows. Without an account the page
// reads only this count.
inline constexpr int kHistorySyncPromoShownByDefault = 1000;

// Rio's Safe Browsing protects through its downloaded lists only: Chromium keeps the hash-prefix real-time lookups to
// Google Chrome builds (hash_realtime_utils.cc), so until the first list update lands every page passes. Chromium
// waits a random 1 to 5 minutes after the service starts before that update; this asks for 10 to 20 seconds
// (--safebrowsing-fast-initial-lists-update), so a fresh install warns on a phishing page within its first minute.
// Later updates keep the server's schedule, and the lists stay on disk for the next launch.
void RequestEarlySafeBrowsingListUpdate();

// The hook EnsureBrowserContextKeyedServiceFactoriesBuilt() calls; it also merges staged site storage (once) and
// installs Rio's overlay scrollbars.
void EnsureCluServiceFactoriesBuilt();

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_MODEL_SERVICE_FACTORY_H_
