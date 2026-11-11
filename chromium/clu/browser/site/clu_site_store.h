// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_SITE_STORE_H_
#define CLU_BROWSER_SITE_CLU_SITE_STORE_H_

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "base/files/file_path.h"
#include "clu/browser/site/clu_tweak_rules.h"
#include "sql/database.h"
#include "sql/meta_table.h"

// One profile's blocking exceptions and Site tweaks (docs/plan/08-extensions.md "DB", docs/chromium/m7.md) in
// <profile>/Clu/site.db, next to the tab model's clu.db. The file belongs to the profile, so rows carry no profile id.
// Rows are read defensively: non-string code reads as '', unreadable zaps as none, junk controls as none, rows whose
// site fails the site key round trip are skipped (and left alone).
namespace clu {

inline constexpr int kCluSiteStoreVersion = 1;
inline constexpr char kCluSiteStoreFileName[] = "site.db";

struct SiteStoreLoad {
  SiteStoreLoad();
  SiteStoreLoad(const SiteStoreLoad&) = delete;
  SiteStoreLoad& operator=(const SiteStoreLoad&) = delete;
  SiteStoreLoad(SiteStoreLoad&&);
  SiteStoreLoad& operator=(SiteStoreLoad&&);
  ~SiteStoreLoad();

  // False when the file could not be opened and changes live in memory for this run.
  bool persistent = true;
  std::set<std::string> exceptions;
  std::map<std::string, SiteTweak> tweaks;
  // The blocker rules version the component extension holds (CluBlocker), empty before the first install.
  std::string blocker_version;
  // Rows skipped for a bad site, and rows whose zaps were unreadable.
  int skipped_rows = 0;
  int junk_zaps = 0;
};

class CluSiteStore {
 public:
  // An empty `path` keeps everything in memory (tests, and the fallback when the file cannot be opened).
  explicit CluSiteStore(base::FilePath path);
  CluSiteStore(const CluSiteStore&) = delete;
  CluSiteStore& operator=(const CluSiteStore&) = delete;
  ~CluSiteStore();

  SiteStoreLoad Load();

  // Adds or removes the exception for `site`. False when nothing was written.
  bool SetException(const std::string& site, bool excepted, int64_t now);
  // Inserts or replaces the site's tweak. False when nothing was written.
  bool PutTweak(const SiteTweak& tweak);
  bool DeleteTweak(const std::string& site);
  bool SetBlockerVersion(const std::string& version);

 private:
  bool Open();
  bool CreateSchema();

  const base::FilePath path_;
  sql::Database db_;
  sql::MetaTable meta_;
  bool open_ = false;
  bool persistent_ = false;
};

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_SITE_STORE_H_
