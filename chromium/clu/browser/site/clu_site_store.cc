// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_store.h"

#include <cmath>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "clu/browser/site/clu_site_key.h"
#include "sql/statement.h"

namespace clu {

namespace {

constexpr char kBlockerVersionKey[] = "clu.blocker_version";

std::string TextOrEmpty(sql::Statement& s, int column) {
  return s.GetColumnType(column) == sql::ColumnType::kText
             ? s.ColumnString(column)
             : std::string();
}

}  // namespace

SiteStoreLoad::SiteStoreLoad() = default;
SiteStoreLoad::SiteStoreLoad(SiteStoreLoad&&) = default;
SiteStoreLoad& SiteStoreLoad::operator=(SiteStoreLoad&&) = default;
SiteStoreLoad::~SiteStoreLoad() = default;

// The tab model's tag: sql::Database::Tag names must be listed in histograms.xml, and one Clu tag keeps the patch to
// one line.
CluSiteStore::CluSiteStore(base::FilePath path)
    : path_(std::move(path)), db_(sql::Database::Tag("CluModel")) {
  db_.set_error_callback(base::BindRepeating(
      [](int error, sql::Statement* statement) {
        LOG(ERROR) << "clu: site.db sqlite error " << error;
      }));
}

CluSiteStore::~CluSiteStore() = default;

bool CluSiteStore::Open() {
  if (open_) {
    return true;
  }
  if (!path_.empty()) {
    if (base::CreateDirectory(path_.DirName()) && db_.Open(path_)) {
      persistent_ = true;
    } else {
      LOG(ERROR) << "clu: cannot open " << path_ << "; site data stays in memory";
    }
  }
  if (!persistent_ && !db_.OpenInMemory()) {
    return false;
  }
  if (sql::MetaTable::RazeIfIncompatible(&db_, kCluSiteStoreVersion,
                                         kCluSiteStoreVersion) ==
      sql::RazeIfIncompatibleResult::kFailed) {
    return false;
  }
  if (!meta_.Init(&db_, kCluSiteStoreVersion, kCluSiteStoreVersion) ||
      !CreateSchema()) {
    return false;
  }
  open_ = true;
  return true;
}

bool CluSiteStore::CreateSchema() {
  // No CHECK constraints: a junk row must still load, so the read can skip or repair it.
  return db_.Execute(
             "CREATE TABLE IF NOT EXISTS blocking_exceptions (site TEXT "
             "PRIMARY KEY, created_at INTEGER)") &&
         db_.Execute(
             "CREATE TABLE IF NOT EXISTS site_tweaks (site TEXT PRIMARY KEY, "
             "enabled INTEGER, css TEXT, js TEXT, controls_json TEXT, zaps "
             "TEXT, updated_at INTEGER)");
}

SiteStoreLoad CluSiteStore::Load() {
  SiteStoreLoad load;
  if (!Open()) {
    load.persistent = false;
    return load;
  }
  load.persistent = persistent_;
  if (!meta_.GetValue(kBlockerVersionKey, &load.blocker_version)) {
    load.blocker_version.clear();
  }
  {
    sql::Statement s(
        db_.GetUniqueStatement("SELECT site FROM blocking_exceptions"));
    while (s.Step()) {
      const std::string site = TextOrEmpty(s, 0);
      if (IsSiteKey(site)) {
        load.exceptions.insert(site);
      } else {
        ++load.skipped_rows;
      }
    }
  }
  {
    sql::Statement s(db_.GetUniqueStatement(
        "SELECT site, enabled, css, js, controls_json, zaps, updated_at FROM "
        "site_tweaks"));
    while (s.Step()) {
      SiteTweak t;
      t.site = TextOrEmpty(s, 0);
      if (!IsSiteKey(t.site)) {
        ++load.skipped_rows;
        continue;
      }
      // Anything but a stored 0 reads as on.
      t.enabled = !(s.GetColumnType(1) == sql::ColumnType::kInteger &&
                    s.ColumnInt(1) == 0) &&
                  !(s.GetColumnType(1) == sql::ColumnType::kText &&
                    s.ColumnString(1) == "0");
      t.css = TextOrEmpty(s, 2);
      t.js = TextOrEmpty(s, 3);
      t.controls = ParseControls(TextOrEmpty(s, 4));
      bool junk = false;
      t.zaps = ParseZaps(TextOrEmpty(s, 5), &junk);
      if (junk) {
        ++load.junk_zaps;
      }
      if (s.GetColumnType(6) == sql::ColumnType::kInteger) {
        t.updated_at = static_cast<double>(s.ColumnInt64(6));
      } else if (s.GetColumnType(6) == sql::ColumnType::kFloat &&
                 std::isfinite(s.ColumnDouble(6))) {
        t.updated_at = s.ColumnDouble(6);
      }
      std::string site = t.site;
      load.tweaks.emplace(std::move(site), std::move(t));
    }
  }
  if (load.skipped_rows || load.junk_zaps) {
    LOG(WARNING) << "clu: site.db skipped " << load.skipped_rows
                 << " rows, read " << load.junk_zaps << " zap lists as none";
  }
  return load;
}

bool CluSiteStore::SetException(const std::string& site,
                                bool excepted,
                                int64_t now) {
  if (!Open() || !IsSiteKey(site)) {
    return false;
  }
  // Two statements, two cache ids: GetCachedStatement keys by call site.
  if (excepted) {
    sql::Statement s(db_.GetCachedStatement(
        SQL_FROM_HERE,
        "INSERT OR REPLACE INTO blocking_exceptions (site, created_at) VALUES "
        "(?, ?)"));
    s.BindString(0, site);
    s.BindInt64(1, now);
    return s.Run();
  }
  sql::Statement s(db_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM blocking_exceptions WHERE site = ?"));
  s.BindString(0, site);
  return s.Run();
}

bool CluSiteStore::PutTweak(const SiteTweak& t) {
  if (!Open() || !IsSiteKey(t.site)) {
    return false;
  }
  sql::Statement s(db_.GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO site_tweaks (site, enabled, css, js, "
      "controls_json, zaps, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)"));
  s.BindString(0, t.site);
  s.BindInt(1, t.enabled ? 1 : 0);
  s.BindString(2, t.css);
  s.BindString(3, t.js);
  if (t.controls && !IsDefaultControls(*t.controls)) {
    s.BindString(4, ControlsToJson(*t.controls));
  } else {
    s.BindNull(4);
  }
  s.BindString(5, ZapsToJson(t.zaps));
  s.BindInt64(6, static_cast<int64_t>(t.updated_at));
  return s.Run();
}

bool CluSiteStore::SetBlockerVersion(const std::string& version) {
  return Open() && meta_.SetValue(kBlockerVersionKey, version);
}

bool CluSiteStore::DeleteTweak(const std::string& site) {
  if (!Open()) {
    return false;
  }
  sql::Statement s(db_.GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM site_tweaks WHERE site = ?"));
  s.BindString(0, site);
  return s.Run();
}

}  // namespace clu
