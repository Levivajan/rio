// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_key.h"

#include "base/strings/strcat.h"
#include "url/gurl.h"

namespace clu {

std::optional<std::string> SiteKey(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      url.spec().size() > kMaxSiteUrlLength) {
    return std::nullopt;
  }
  std::string host(url.host());
  if (host.empty()) {
    return std::nullopt;
  }
  // GURL lowercases and canonicalizes the host already.
  if (host.starts_with("www.") && host.size() > 4) {
    host.erase(0, 4);
  }
  return host;
}

bool IsSiteKey(std::string_view site) {
  if (site.empty() || site.size() > kMaxSiteUrlLength) {
    return false;
  }
  const std::optional<std::string> key =
      SiteKey(GURL(base::StrCat({"http://", site, "/"})));
  return key && *key == site;
}

bool DevModeDefault(std::string_view site) {
  return site == "localhost" || site.ends_with(".localhost") ||
         site == "127.0.0.1" || site == "[::1]";
}

bool HostMatchesDomain(std::string_view host, std::string_view domain) {
  if (domain.empty() || host.size() < domain.size()) {
    return false;
  }
  if (host == domain) {
    return true;
  }
  return host.ends_with(domain) && host[host.size() - domain.size() - 1] == '.';
}

}  // namespace clu
