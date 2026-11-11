// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_icon_backfill.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_types.h"
#include "components/keyed_service/core/service_access_type.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/decode_image.h"
#include "services/data_decoder/public/mojom/image_decoder.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"

namespace clu {

namespace {

constexpr char kVersion[] = "f";
// The page's HTML is read only for its head; more than this is not needed.
constexpr size_t kMaxPageBytes = 512 * 1024;
// An icon file bigger than this is not an icon.
constexpr size_t kMaxIconBytes = 256 * 1024;
// A decoded icon is shrunk to fit this (256 x 256 RGBA).
constexpr uint64_t kMaxDecodedBytes = 256 * 256 * 4;
// Declared icons tried before /favicon.ico.
constexpr size_t kMaxDeclaredTries = 3;
constexpr base::TimeDelta kFetchTimeout = base::Seconds(15);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rio_icon_backfill", R"(
        semantics {
          sender: "Rio sidebar icons"
          description:
            "Reads the HTML of a favorite, pinned or Today tab the user has "
            "not opened in this profile yet, for the icons it declares, and "
            "fetches the best of them or the site's /favicon.ico, so the "
            "sidebar shows the site's icon instead of a globe. No script runs "
            "and nothing is rendered."
          trigger:
            "A window shows a tab without a page whose site has no icon in the "
            "browser's favicon database. Once per site per launch."
          data: "None. No cookies or credentials are sent."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Remove the tab."
          policy_exception_justification: "Not implemented."
        })");

std::unique_ptr<network::SimpleURLLoader> MakeLoader(const GURL& url) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(kFetchTimeout);
  return loader;
}

}  // namespace

struct CluIconBackfill::Job {
  GURL page_url;
  url::Origin origin;
  std::vector<GURL> icon_urls;
  size_t next = 0;
  std::unique_ptr<network::SimpleURLLoader> loader;
};

CluIconBackfill::CluIconBackfill(Profile* profile,
                                 base::RepeatingClosure on_icon)
    : profile_(profile), on_icon_(std::move(on_icon)) {}

CluIconBackfill::~CluIconBackfill() = default;

std::string_view CluIconBackfill::Version(const GURL& page_url) {
  if (!IsWebUrl(page_url) || !profile_ || profile_->IsOffTheRecord()) {
    return std::string_view();
  }
  const url::Origin origin = url::Origin::Create(page_url);
  if (auto it = origins_.find(origin); it != origins_.end()) {
    return it->second == State::kStored ? std::string_view(kVersion)
                                         : std::string_view();
  }
  favicon::FaviconService* favicons = FaviconServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicons || origins_.size() >= kMaxOrigins) {
    return std::string_view();
  }
  origins_.emplace(origin, State::kChecking);
  // As favicon2 asks: any icon of the host will do.
  favicons->GetRawFaviconForPageURL(
      page_url, {favicon_base::IconType::kFavicon}, /*desired_size_in_pixel=*/0,
      /*fallback_to_host=*/true,
      base::BindOnce(&CluIconBackfill::OnLookup, weak_factory_.GetWeakPtr(),
                     page_url),
      &tracker_);
  return std::string_view();
}

void CluIconBackfill::OnLookup(
    const GURL& page_url,
    const favicon_base::FaviconRawBitmapResult& result) {
  const url::Origin origin = url::Origin::Create(page_url);
  if (result.is_valid()) {
    origins_[origin] = State::kDone;
    return;
  }
  origins_[origin] = State::kFetching;
  queue_.push_back(page_url);
  Pump();
}

void CluIconBackfill::Pump() {
  while (jobs_.size() < kMaxActive && !queue_.empty()) {
    auto job = std::make_unique<Job>();
    job->page_url = std::move(queue_.front());
    queue_.pop_front();
    job->origin = url::Origin::Create(job->page_url);
    job->loader = MakeLoader(job->page_url);
    Job* raw = job.get();
    jobs_.push_back(std::move(job));
    scoped_refptr<network::SharedURLLoaderFactory> factory =
        profile_->GetDefaultStoragePartition()
            ->GetURLLoaderFactoryForBrowserProcess();
    raw->loader->DownloadToString(
        factory.get(),
        base::BindOnce(&CluIconBackfill::OnPage, weak_factory_.GetWeakPtr(),
                       raw),
        kMaxPageBytes);
  }
}

void CluIconBackfill::OnPage(Job* job, std::optional<std::string> body) {
  const network::mojom::URLResponseHead* head = job->loader->ResponseInfo();
  if (body && head && head->mime_type == "text/html") {
    job->icon_urls = DeclaredIconUrls(*body, job->loader->GetFinalURL());
    if (job->icon_urls.size() > kMaxDeclaredTries) {
      job->icon_urls.resize(kMaxDeclaredTries);
    }
  }
  const GURL favicon_ico = job->origin.GetURL().Resolve("/favicon.ico");
  if (std::find(job->icon_urls.begin(), job->icon_urls.end(), favicon_ico) ==
      job->icon_urls.end()) {
    job->icon_urls.push_back(favicon_ico);
  }
  TryNextIcon(job);
}

void CluIconBackfill::TryNextIcon(Job* job) {
  if (job->next >= job->icon_urls.size()) {
    Finish(job, /*stored=*/false);
    return;
  }
  const GURL icon_url = job->icon_urls[job->next++];
  job->loader = MakeLoader(icon_url);
  scoped_refptr<network::SharedURLLoaderFactory> factory =
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess();
  job->loader->DownloadToString(
      factory.get(),
      base::BindOnce(&CluIconBackfill::OnIconBytes, weak_factory_.GetWeakPtr(),
                     job, icon_url),
      kMaxIconBytes);
}

void CluIconBackfill::OnIconBytes(Job* job,
                                  const GURL& icon_url,
                                  std::optional<std::string> body) {
  job->loader.reset();
  if (!body || body->empty()) {
    TryNextIcon(job);
    return;
  }
  // A .ico holds several sizes: the one nearest the 16 pt slot at 2x.
  data_decoder::DecodeImageIsolated(
      base::as_byte_span(*body), data_decoder::mojom::ImageCodec::kDefault,
      /*shrink_to_fit=*/true, kMaxDecodedBytes, gfx::Size(32, 32),
      base::BindOnce(&CluIconBackfill::OnDecoded, weak_factory_.GetWeakPtr(),
                     job, icon_url));
}

void CluIconBackfill::OnDecoded(Job* job,
                                const GURL& icon_url,
                                const SkBitmap& bitmap) {
  favicon::FaviconService* favicons = FaviconServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!favicons) {
    Finish(job, /*stored=*/false);
    return;
  }
  if (bitmap.drawsNothing()) {
    TryNextIcon(job);
    return;
  }
  favicons->SetOnDemandFavicons(
      job->page_url, icon_url, favicon_base::IconType::kFavicon,
      gfx::Image::CreateFrom1xBitmap(bitmap),
      base::BindOnce(&CluIconBackfill::Finish, weak_factory_.GetWeakPtr(),
                     job));
}

void CluIconBackfill::Finish(Job* job, bool stored) {
  origins_[job->origin] = stored ? State::kStored : State::kDone;
  std::erase_if(jobs_, [job](const std::unique_ptr<Job>& j) {
    return j.get() == job;
  });
  Pump();
  if (stored && on_icon_) {
    on_icon_.Run();
  }
}

}  // namespace clu
