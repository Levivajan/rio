// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_SERVICE_H_
#define CLU_BROWSER_SITE_CLU_SITE_SERVICE_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/threading/sequence_bound.h"
#include "clu/browser/site/clu_site_store.h"
#include "clu/browser/site/clu_tweak_rules.h"

namespace base {
class Clock;
}  // namespace base

// One profile's blocking exceptions and Site tweaks (docs/plan/08-extensions.md F7, F9 to F11). Pages, the blocker and
// the windows listen through Observer. Rows live in CluSiteStore on a blocking sequence; the service keeps them in
// memory, so reads never wait. Changes before the rows are read are refused (the blocker waits for the read before any
// page loads).
namespace clu {

class CluSiteService {
 public:
  class Observer : public base::CheckedObserver {
   public:
    // The rows are read.
    virtual void OnSiteDataLoaded() {}
    // Blocking was turned on or off for `site`.
    virtual void OnBlockingChanged(const std::string& site) {}
    // The tweak of `site` changed (saved, toggled, zapped, deleted).
    virtual void OnTweakChanged(const std::string& site) {}
  };

  // `store_path` empty keeps the rows in memory. `clock` null is the default clock.
  CluSiteService(base::FilePath store_path, const base::Clock* clock);
  CluSiteService(const CluSiteService&) = delete;
  CluSiteService& operator=(const CluSiteService&) = delete;
  ~CluSiteService();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  bool loaded() const { return loaded_; }
  // False when site.db could not be opened and changes last for this run.
  bool persistent() const { return persistent_; }
  // Runs `callback` once the rows are read (at once when they are).
  void WhenLoaded(base::OnceClosure callback);

  // ---- blocking (F7) ---------------------------------------------------------------------------------------------

  bool IsBlockingEnabled(std::string_view site) const;
  const std::set<std::string>& exceptions() const { return exceptions_; }
  // Turns blocking on or off for `site` and stores it. False when nothing changed (or before the read, or a bad site).
  bool SetBlockingEnabled(const std::string& site, bool enabled);

  // ---- Site tweaks (F9 to F11) -----------------------------------------------------------------------------------

  // The site's tweak, or null.
  const SiteTweak* Tweak(std::string_view site) const;
  // Every site with something in its tweak, by site.
  std::vector<SiteTweak> ListTweaks() const;

  // tweaks.save: CSS and JS (each at most 256 KiB), and the controls when given; enabled and zaps stay. Creates the
  // tweak, on. Returns the tweak as it was (nullopt for a new one); a refused save (size, read pending) returns false
  // in `ok`.
  struct SaveResult {
    bool ok = false;
    // The JS before the save, to decide on a reload (a script that already ran cannot be taken back).
    std::string old_js;
    bool enabled = true;
  };
  SaveResult SaveTweak(const std::string& site,
                       std::string css,
                       std::string js,
                       const std::optional<TweakControls>& controls);
  // Turns the site's tweak on or off; on creates an empty tweak for a site without one. Returns whether it has JS (its
  // pages reload), or nullopt when nothing changed (off with no tweak, already so).
  std::optional<bool> SetTweakEnabled(const std::string& site, bool enabled);
  // Appends a zap, creating the tweak (on) when needed. A zap without createdAt gets the clock's now.
  AppendZapResult AddZap(const std::string& site, Zap zap);
  // Pops the last zap; nullopt when there is none.
  std::optional<Zap> UndoZap(const std::string& site);
  // Drops one zap; false for an index past the end.
  bool RemoveZap(const std::string& site, size_t index);
  // Drops every zap; false when there was none.
  bool ClearZaps(const std::string& site);
  // Deletes the site's tweak. Returns it as it was, or nullopt when there was none.
  std::optional<SiteTweak> DeleteTweak(const std::string& site);

  // The blocker's rules version stored with the rows (the rules the component extension holds now).
  const std::string& blocker_version() const { return blocker_version_; }
  void SetBlockerVersion(const std::string& version);

  // Tests: runs `callback` once every write posted so far has landed.
  void FlushForTesting(base::OnceClosure callback);

 private:
  void OnLoaded(SiteStoreLoad load);
  void Write(const SiteTweak& tweak);
  void NotifyTweak(const std::string& site);
  double NowMs() const;

  raw_ptr<const base::Clock> clock_;
  base::SequenceBound<CluSiteStore> store_;
  bool loaded_ = false;
  bool persistent_ = true;
  std::vector<base::OnceClosure> on_loaded_;
  std::set<std::string> exceptions_;
  std::map<std::string, SiteTweak, std::less<>> tweaks_;
  std::string blocker_version_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<CluSiteService> weak_ptr_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_SERVICE_H_
