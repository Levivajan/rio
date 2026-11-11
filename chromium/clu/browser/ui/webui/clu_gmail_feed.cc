// Copyright 2026 Levon Adamyan. SPDX-License-Identifier: GPL-3.0-only

#include "clu/browser/ui/webui/clu_gmail_feed.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/net_errors.h"
#include "net/cookies/site_for_cookies.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/origin.h"

namespace clu {

namespace {

// A feed larger than this is no Gmail feed (Gmail lists at most 20 entries).
constexpr size_t kMaxFeedBytes = 512 * 1024;

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("rio_gmail_unread", R"(
        semantics {
          sender: "Rio Gmail preview card"
          description:
            "Reads the Atom feed of the Gmail account a Gmail favorite or "
            "pinned tab is signed in as, to show its unread count and newest "
            "unread messages in the card that opens when the user hovers it."
          trigger:
            "The user hovers a Gmail favorite or pinned tab; at most once a "
            "minute per account."
          data:
            "The user's Google cookies for mail.google.com, as the Gmail tab "
            "itself sends them."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "Remove the Gmail favorite or pinned tab."
          policy_exception_justification: "Not implemented."
        })");

GURL& FeedOriginForTesting() {
  static base::NoDestructor<GURL> origin;
  return *origin;
}

}  // namespace

CluGmailFeed::CluGmailFeed() = default;
CluGmailFeed::~CluGmailFeed() = default;

// static
void CluGmailFeed::SetFeedOriginForTesting(const GURL& origin) {
  FeedOriginForTesting() = origin;
}

const CluGmailFeed::Result* CluGmailFeed::Find(
    content::BrowserContext* context,
    const GURL& feed) const {
  const auto it = results_.find(Key(context, feed.spec()));
  return it == results_.end() ? nullptr : &it->second;
}

bool CluGmailFeed::Stale(content::BrowserContext* context,
                         const GURL& feed) const {
  const Result* result = Find(context, feed);
  return !result || base::TimeTicks::Now() - result->at >= kGmailFeedFresh;
}

void CluGmailFeed::Fetch(content::BrowserContext* context,
                         const GURL& feed,
                         base::OnceClosure done) {
  Key key(context, feed.spec());
  std::vector<base::OnceClosure>& waiting = waiting_[key];
  waiting.push_back(std::move(done));
  if (waiting.size() > 1) {
    return;
  }
  // No profile or no feed: an answer that is no answer, so the card falls back to the title at once.
  if (!context || !feed.is_valid()) {
    Finish(key, std::nullopt);
    return;
  }
  GURL url = feed;
  if (const GURL& origin = FeedOriginForTesting(); origin.is_valid()) {
    GURL::Replacements to_test;
    to_test.SetSchemeStr(origin.scheme());
    to_test.SetHostStr(origin.host());
    to_test.SetPortStr(origin.port());
    url = url.ReplaceComponents(to_test);
  }
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->method = "GET";
  // The account's own sign-in: the row's profile's cookies for Gmail, sent first-party as the Gmail tab sends them.
  request->credentials_mode = network::mojom::CredentialsMode::kInclude;
  request->request_initiator = url::Origin::Create(url);
  request->site_for_cookies = net::SiteForCookies::FromUrl(url);
  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader->SetTimeoutDuration(kGmailFeedTimeout);
  network::SimpleURLLoader* raw = loader.get();
  loaders_.insert(std::move(loader));
  raw->DownloadToString(
      context->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&CluGmailFeed::OnFetched, weak_ptr_factory_.GetWeakPtr(),
                     std::move(key), raw),
      kMaxFeedBytes);
}

void CluGmailFeed::OnFetched(Key key,
                             network::SimpleURLLoader* loader,
                             std::optional<std::string> body) {
  const bool ok_status = loader->NetError() == net::OK &&
                         loader->ResponseInfo() &&
                         loader->ResponseInfo()->headers &&
                         loader->ResponseInfo()->headers->response_code() == 200;
  loaders_.erase(loaders_.find(loader));
  if (!ok_status || !body || body->empty()) {
    Finish(key, std::nullopt);
    return;
  }
  data_decoder::DataDecoder::ParseXmlIsolated(
      *body, data_decoder::mojom::XmlParser::WhitespaceBehavior::kIgnore,
      base::BindOnce(&CluGmailFeed::OnParsed, weak_ptr_factory_.GetWeakPtr(),
                     std::move(key)));
}

void CluGmailFeed::OnParsed(Key key,
                            base::expected<base::Value, std::string> parsed) {
  Finish(key, parsed.has_value() ? ParseGmailFeed(*parsed) : std::nullopt);
}

void CluGmailFeed::Finish(const Key& key, std::optional<GmailUnread> unread) {
  Result& result = results_[key];
  // A failed read keeps the last good answer's list but marks it not ok, so the card falls back to the title's count.
  result.ok = unread.has_value();
  if (unread) {
    result.unread = std::move(*unread);
  }
  result.at = base::TimeTicks::Now();
  std::vector<base::OnceClosure> waiting = std::move(waiting_[key]);
  waiting_.erase(key);
  for (base::OnceClosure& done : waiting) {
    std::move(done).Run();
  }
}

}  // namespace clu
