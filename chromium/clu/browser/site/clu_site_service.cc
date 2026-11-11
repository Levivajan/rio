// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "clu/browser/site/clu_site_key.h"

namespace clu {

CluSiteService::CluSiteService(base::FilePath store_path,
                               const base::Clock* clock)
    : clock_(clock ? clock : base::DefaultClock::GetInstance()),
      store_(base::ThreadPool::CreateSequencedTaskRunner(
                 {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
             std::move(store_path)) {
  store_.AsyncCall(&CluSiteStore::Load)
      .Then(base::BindOnce(&CluSiteService::OnLoaded,
                           weak_ptr_factory_.GetWeakPtr()));
}

CluSiteService::~CluSiteService() = default;

void CluSiteService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CluSiteService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void CluSiteService::WhenLoaded(base::OnceClosure callback) {
  if (loaded_) {
    std::move(callback).Run();
    return;
  }
  on_loaded_.push_back(std::move(callback));
}

void CluSiteService::OnLoaded(SiteStoreLoad load) {
  persistent_ = load.persistent;
  exceptions_ = std::move(load.exceptions);
  for (auto& [site, tweak] : load.tweaks) {
    tweaks_.emplace(site, std::move(tweak));
  }
  blocker_version_ = std::move(load.blocker_version);
  loaded_ = true;
  for (Observer& observer : observers_) {
    observer.OnSiteDataLoaded();
  }
  std::vector<base::OnceClosure> callbacks = std::move(on_loaded_);
  for (base::OnceClosure& callback : callbacks) {
    std::move(callback).Run();
  }
}

double CluSiteService::NowMs() const {
  return clock_->Now().InMillisecondsFSinceUnixEpoch();
}

bool CluSiteService::IsBlockingEnabled(std::string_view site) const {
  return !exceptions_.contains(std::string(site));
}

bool CluSiteService::SetBlockingEnabled(const std::string& site,
                                        bool enabled) {
  if (!loaded_ || !IsSiteKey(site) || IsBlockingEnabled(site) == enabled) {
    return false;
  }
  if (enabled) {
    exceptions_.erase(site);
  } else {
    exceptions_.insert(site);
  }
  store_.AsyncCall(&CluSiteStore::SetException)
      .WithArgs(site, !enabled, static_cast<int64_t>(NowMs()))
      .Then(base::BindOnce([](bool) {}));
  for (Observer& observer : observers_) {
    observer.OnBlockingChanged(site);
  }
  return true;
}

const SiteTweak* CluSiteService::Tweak(std::string_view site) const {
  auto it = tweaks_.find(site);
  return it == tweaks_.end() ? nullptr : &it->second;
}

std::vector<SiteTweak> CluSiteService::ListTweaks() const {
  std::vector<SiteTweak> out;
  for (const auto& [site, tweak] : tweaks_) {
    if (!IsEmptyTweak(tweak)) {
      out.push_back(tweak);
    }
  }
  return out;
}

void CluSiteService::Write(const SiteTweak& tweak) {
  store_.AsyncCall(&CluSiteStore::PutTweak)
      .WithArgs(tweak)
      .Then(base::BindOnce([](bool) {}));
}

void CluSiteService::NotifyTweak(const std::string& site) {
  for (Observer& observer : observers_) {
    observer.OnTweakChanged(site);
  }
}

CluSiteService::SaveResult CluSiteService::SaveTweak(
    const std::string& site,
    std::string css,
    std::string js,
    const std::optional<TweakControls>& controls) {
  SaveResult result;
  if (!loaded_ || !IsSiteKey(site) || !IsTweakCodeSizeOk(css, js)) {
    return result;
  }
  SiteTweak& tweak = tweaks_[site];
  result.old_js = tweak.js;
  if (tweak.site.empty()) {
    tweak.site = site;
    tweak.enabled = true;
  }
  tweak.css = std::move(css);
  tweak.js = std::move(js);
  if (controls) {
    // Controls that change nothing store as none, so "Reset" leaves no tweak behind.
    tweak.controls = IsDefaultControls(*controls)
                         ? std::nullopt
                         : std::optional<TweakControls>(*controls);
  }
  tweak.updated_at = NowMs();
  result.ok = true;
  result.enabled = tweak.enabled;
  Write(tweak);
  NotifyTweak(site);
  return result;
}

std::optional<bool> CluSiteService::SetTweakEnabled(const std::string& site,
                                                    bool enabled) {
  if (!loaded_) {
    return std::nullopt;
  }
  auto it = tweaks_.find(site);
  // The panel's switch turns a site without a tweak on: an empty tweak, on, that the editor and Zap fill.
  if (it == tweaks_.end()) {
    if (!enabled || !IsSiteKey(site)) {
      return std::nullopt;
    }
    it = tweaks_.emplace(site, SiteTweak()).first;
    it->second.site = site;
    it->second.enabled = !enabled;
  }
  if (it->second.enabled == enabled) {
    return std::nullopt;
  }
  it->second.enabled = enabled;
  it->second.updated_at = NowMs();
  Write(it->second);
  NotifyTweak(site);
  return !base::TrimWhitespaceASCII(it->second.js, base::TRIM_ALL).empty();
}

AppendZapResult CluSiteService::AddZap(const std::string& site, Zap zap) {
  if (!loaded_ || !IsSiteKey(site)) {
    return AppendZapResult::kUnsafe;
  }
  if (zap.created_at <= 0) {
    zap.created_at = NowMs();
  }
  auto it = tweaks_.find(site);
  std::vector<Zap> zaps = it == tweaks_.end() ? std::vector<Zap>()
                                              : it->second.zaps;
  const AppendZapResult result = AppendZap(zaps, std::move(zap));
  if (result != AppendZapResult::kOk) {
    return result;
  }
  SiteTweak& tweak = tweaks_[site];
  if (tweak.site.empty()) {
    tweak.site = site;
    tweak.enabled = true;
  }
  tweak.zaps = std::move(zaps);
  tweak.updated_at = NowMs();
  Write(tweak);
  NotifyTweak(site);
  return result;
}

std::optional<Zap> CluSiteService::UndoZap(const std::string& site) {
  auto it = tweaks_.find(site);
  if (!loaded_ || it == tweaks_.end() || it->second.zaps.empty()) {
    return std::nullopt;
  }
  Zap last = std::move(it->second.zaps.back());
  it->second.zaps.pop_back();
  it->second.updated_at = NowMs();
  Write(it->second);
  NotifyTweak(site);
  return last;
}

bool CluSiteService::RemoveZap(const std::string& site, size_t index) {
  auto it = tweaks_.find(site);
  if (!loaded_ || it == tweaks_.end() || index >= it->second.zaps.size()) {
    return false;
  }
  it->second.zaps.erase(it->second.zaps.begin() + index);
  it->second.updated_at = NowMs();
  Write(it->second);
  NotifyTweak(site);
  return true;
}

bool CluSiteService::ClearZaps(const std::string& site) {
  auto it = tweaks_.find(site);
  if (!loaded_ || it == tweaks_.end() || it->second.zaps.empty()) {
    return false;
  }
  it->second.zaps.clear();
  it->second.updated_at = NowMs();
  Write(it->second);
  NotifyTweak(site);
  return true;
}

std::optional<SiteTweak> CluSiteService::DeleteTweak(const std::string& site) {
  auto it = tweaks_.find(site);
  if (!loaded_ || it == tweaks_.end()) {
    return std::nullopt;
  }
  SiteTweak old = std::move(it->second);
  tweaks_.erase(it);
  store_.AsyncCall(&CluSiteStore::DeleteTweak)
      .WithArgs(site)
      .Then(base::BindOnce([](bool) {}));
  NotifyTweak(site);
  return old;
}

void CluSiteService::SetBlockerVersion(const std::string& version) {
  blocker_version_ = version;
  store_.AsyncCall(&CluSiteStore::SetBlockerVersion)
      .WithArgs(version)
      .Then(base::BindOnce([](bool) {}));
}

void CluSiteService::FlushForTesting(base::OnceClosure callback) {
  store_.AsyncCall(&CluSiteStore::SetBlockerVersion)
      .WithArgs(blocker_version_)
      .Then(base::BindOnce([](base::OnceClosure done, bool) {
        std::move(done).Run();
      }, std::move(callback)));
}

}  // namespace clu
