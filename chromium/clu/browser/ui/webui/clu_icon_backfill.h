// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_ICON_BACKFILL_H_
#define CLU_BROWSER_UI_WEBUI_CLU_ICON_BACKFILL_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "url/gurl.h"
#include "url/origin.h"

class Profile;
class SkBitmap;

namespace favicon_base {
struct FaviconRawBitmapResult;
}  // namespace favicon_base

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace clu {

// A row never opened in this profile (seeded, imported or restored) has no icon in Chromium's favicon
// database, so favicon2 answers the default globe. The backfill looks each such row's origin up once; when the
// database has nothing for it, it reads the row's page HTML through the profile's own network context (no cookies, no
// scripts run, nothing rendered), tries the icons it declares (largest suitable first) and then /favicon.ico, decodes
// each in the sandboxed data decoder and stores the first that decodes as an on-demand favicon under the row's URL,
// which the first real visit replaces. A few origins at a time, each tried once per launch. Then `on_icon` runs, and
// Version() changes the row's favicon2 URL so the frame asks again.
class CluIconBackfill {
 public:
  // At most this many origins are looked up for one window; later ones keep the globe.
  static constexpr size_t kMaxOrigins = 512;
  // At most this many origins fetch at once.
  static constexpr size_t kMaxActive = 3;

  CluIconBackfill(Profile* profile, base::RepeatingClosure on_icon);
  CluIconBackfill(const CluIconBackfill&) = delete;
  CluIconBackfill& operator=(const CluIconBackfill&) = delete;
  ~CluIconBackfill();

  // The icon version for the favicon2 URL of a row without a page on `page_url`: empty until a fetched icon was
  // stored, then "f". The first call for an http(s) origin starts its lookup.
  std::string_view Version(const GURL& page_url);

 private:
  enum class State { kChecking, kFetching, kStored, kDone };
  struct Job;

  void OnLookup(const GURL& page_url,
                const favicon_base::FaviconRawBitmapResult& result);
  void Pump();
  void OnPage(Job* job, std::optional<std::string> body);
  void TryNextIcon(Job* job);
  void OnIconBytes(Job* job,
                   const GURL& icon_url,
                   std::optional<std::string> body);
  void OnDecoded(Job* job, const GURL& icon_url, const SkBitmap& bitmap);
  void Finish(Job* job, bool stored);

  raw_ptr<Profile> profile_;
  base::RepeatingClosure on_icon_;
  std::map<url::Origin, State> origins_;
  std::deque<GURL> queue_;
  std::vector<std::unique_ptr<Job>> jobs_;
  base::CancelableTaskTracker tracker_;
  base::WeakPtrFactory<CluIconBackfill> weak_factory_{this};
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_ICON_BACKFILL_H_
