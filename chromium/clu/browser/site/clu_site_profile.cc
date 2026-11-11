// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_profile.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/supports_user_data.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "base/scoped_observation.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/site/clu_blocker.h"
#include "clu/browser/site/clu_site_page.h"
#include "clu/browser/site/clu_site_service.h"
#include "clu/browser/site/clu_site_store.h"

namespace clu {

namespace {

const void* const kUserDataKey = &kUserDataKey;
const base::Clock* g_clock_for_testing = nullptr;

// The site.db of the tab model's folder (CluStore's kCluStoreDirName).
constexpr char kSiteStoreDir[] = "Clu";

// Owns the profile's site data and blocker, and carries tweak changes to the pages: the site's CSS is re-inserted in
// every page of it, and every page follows Advanced's Site tweaks switch.
class CluSiteProfileData : public base::SupportsUserData::Data,
                           public ProfileObserver,
                           public CluSiteService::Observer,
                           public CluAppSettings::Observer {
 public:
  explicit CluSiteProfileData(Profile* profile)
      : site_(std::make_unique<CluSiteService>(
            profile->GetPath()
                .AppendASCII(kSiteStoreDir)
                .AppendASCII(kCluSiteStoreFileName),
            g_clock_for_testing)),
        blocker_(std::make_unique<CluBlocker>(profile, site_.get())),
        profile_(profile),
        tweaks_enabled_(CluSitePage::TweaksEnabled()) {
    observation_.Observe(profile);
    site_->AddObserver(this);
    // CluAppSettings never goes (a NoDestructor), so the observation may outlive nothing.
    if (CluAppSettings* settings = CluAppSettings::Get()) {
      settings_observation_.Observe(settings);
    }
  }
  ~CluSiteProfileData() override { site_->RemoveObserver(this); }

  CluSiteService* site() { return site_.get(); }
  CluBlocker* blocker() { return blocker_.get(); }
  bool shut_down() const { return shut_down_; }

  // ProfileObserver: the profile's keyed services go before its user data, and the blocker holds some.
  void OnProfileWillBeDestroyed(Profile* profile) override {
    observation_.Reset();
    settings_observation_.Reset();
    shut_down_ = true;
    blocker_.reset();
  }

  // CluSiteService::Observer (a blocking change reloads its pages from CluBlocker, once the exception is in place):
  void OnTweakChanged(const std::string& site) override {
    CluSitePage::OnTweakChanged(profile_, site);
  }

  // CluAppSettings::Observer: any global setting; only a flip of tweaks.enabled reaches the pages.
  void OnAppSettingsChanged() override {
    const bool enabled = CluSitePage::TweaksEnabled();
    if (enabled == tweaks_enabled_ || shut_down_) {
      return;
    }
    tweaks_enabled_ = enabled;
    CluSitePage::OnTweaksSwitched(profile_);
  }

 private:
  std::unique_ptr<CluSiteService> site_;
  std::unique_ptr<CluBlocker> blocker_;
  raw_ptr<Profile> profile_;
  bool shut_down_ = false;
  bool tweaks_enabled_;
  base::ScopedObservation<Profile, ProfileObserver> observation_{this};
  base::ScopedObservation<CluAppSettings, CluAppSettings::Observer>
      settings_observation_{this};
};

CluSiteProfileData* GetData(content::BrowserContext* context) {
  if (!context) {
    return nullptr;
  }
  Profile* profile = Profile::FromBrowserContext(context)->GetOriginalProfile();
  if (!profile->IsRegularProfile()) {
    return nullptr;
  }
  auto* data =
      static_cast<CluSiteProfileData*>(profile->GetUserData(kUserDataKey));
  if (!data) {
    auto owned = std::make_unique<CluSiteProfileData>(profile);
    data = owned.get();
    profile->SetUserData(kUserDataKey, std::move(owned));
  }
  return data->shut_down() ? nullptr : data;
}

}  // namespace

CluSiteService* GetCluSiteService(content::BrowserContext* context) {
  CluSiteProfileData* data = GetData(context);
  return data ? data->site() : nullptr;
}

CluBlocker* GetCluBlocker(content::BrowserContext* context) {
  CluSiteProfileData* data = GetData(context);
  return data ? data->blocker() : nullptr;
}

void SetCluSiteClockForTesting(const base::Clock* clock) {
  g_clock_for_testing = clock;
}

}  // namespace clu
