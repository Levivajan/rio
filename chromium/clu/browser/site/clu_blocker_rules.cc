// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_blocker_rules.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "clu/browser/site/clu_site_key.h"
#include "clu/browser/site/clu_tweak_rules.h"

namespace clu {

namespace {

// The resources are compiled by the repo's build, but they are still read like any stored data.
constexpr size_t kMaxHost = 253;

bool IsHost(std::string_view host) {
  if (host.empty() || host.size() > kMaxHost || host.front() == '.' ||
      host.back() == '.' || host.front() == '-' ||
      host.find("..") != std::string_view::npos) {
    return false;
  }
  for (char c : host) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
          c == '.')) {
      return false;
    }
  }
  return true;
}

void AppendRule(std::string& css, std::string_view selector) {
  base::StrAppend(&css, {selector, "{display:none!important}\n"});
}

// `host`, then each parent domain: a.b.c, b.c, c.
std::vector<std::string_view> DomainsOf(std::string_view host) {
  std::vector<std::string_view> out;
  while (!host.empty()) {
    out.push_back(host);
    const size_t dot = host.find('.');
    if (dot == std::string_view::npos) {
      break;
    }
    host.remove_prefix(dot + 1);
  }
  return out;
}

}  // namespace

BlockerRules::BlockerRules() = default;
BlockerRules::BlockerRules(BlockerRules&&) = default;
BlockerRules& BlockerRules::operator=(BlockerRules&&) = default;
BlockerRules::~BlockerRules() = default;

CosmeticData::CosmeticData() = default;
CosmeticData::CosmeticData(const CosmeticData&) = default;
CosmeticData& CosmeticData::operator=(const CosmeticData&) = default;
CosmeticData::CosmeticData(CosmeticData&&) = default;
CosmeticData& CosmeticData::operator=(CosmeticData&&) = default;
CosmeticData::~CosmeticData() = default;

bool IsBlockerVersion(std::string_view version) {
  if (version.size() != 16) {
    return false;
  }
  for (char c : version) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

std::optional<BlockerRules> ParseBlockerRules(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* version = dict->FindString("version");
  base::ListValue* rules = dict->FindList("rules");
  if (!version || !IsBlockerVersion(*version) || !rules) {
    return std::nullopt;
  }
  BlockerRules out;
  out.version = *version;
  std::set<int> ids;
  for (base::Value& rule : *rules) {
    base::DictValue* d = rule.GetIfDict();
    const std::optional<int> id = d ? d->FindInt("id") : std::nullopt;
    if (!id || *id < 1 || *id > kMaxBlockerRules || !ids.insert(*id).second ||
        !d->FindDict("action") || !d->FindDict("condition")) {
      ++out.skipped;
      continue;
    }
    out.rules.Append(std::move(rule));
  }
  return out;
}

std::optional<CosmeticData> ParseCosmeticData(std::string_view json) {
  std::optional<base::DictValue> dict =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!dict) {
    return std::nullopt;
  }
  const std::string* version = dict->FindString("version");
  if (!version || !IsBlockerVersion(*version)) {
    return std::nullopt;
  }
  CosmeticData out;
  out.version = *version;
  auto selectors = [](const base::Value& list) {
    std::vector<std::string> kept;
    if (!list.is_list()) {
      return kept;
    }
    for (const base::Value& s : list.GetList()) {
      if (s.is_string() && IsSafeSelector(s.GetString())) {
        kept.push_back(s.GetString());
      }
    }
    return kept;
  };
  if (const base::Value* generic = dict->Find("generic")) {
    out.generic = selectors(*generic);
  }
  if (const base::DictValue* domains = dict->FindDict("domains")) {
    for (const auto [host, list] : *domains) {
      if (IsHost(host)) {
        std::vector<std::string> kept = selectors(list);
        if (!kept.empty()) {
          out.domains.emplace(host, std::move(kept));
        }
      }
    }
  }
  if (const base::DictValue* exceptions = dict->FindDict("exceptions")) {
    for (const auto [host, list] : *exceptions) {
      if (IsHost(host)) {
        std::vector<std::string> kept = selectors(list);
        out.exceptions.emplace(host, std::set<std::string>(kept.begin(),
                                                           kept.end()));
      }
    }
  }
  if (const base::ListValue* hide = dict->FindList("genericHide")) {
    for (const base::Value& host : *hide) {
      if (host.is_string() && IsHost(host.GetString())) {
        out.generic_hide.insert(host.GetString());
      }
    }
  }
  for (const std::string& selector : out.generic) {
    AppendRule(out.generic_css, selector);
  }
  return out;
}

std::string CosmeticCssFor(const CosmeticData& data, std::string_view host) {
  const std::vector<std::string_view> domains = DomainsOf(host);
  bool generic = true;
  std::set<std::string_view> excepted;
  for (std::string_view domain : domains) {
    if (data.generic_hide.contains(domain)) {
      generic = false;
    }
    if (auto it = data.exceptions.find(domain); it != data.exceptions.end()) {
      excepted.insert(it->second.begin(), it->second.end());
    }
  }
  std::string css;
  if (generic && excepted.empty()) {
    css = data.generic_css;
  } else if (generic) {
    for (const std::string& selector : data.generic) {
      if (!excepted.contains(selector)) {
        AppendRule(css, selector);
      }
    }
  }
  for (std::string_view domain : domains) {
    auto it = data.domains.find(domain);
    if (it == data.domains.end()) {
      continue;
    }
    for (const std::string& selector : it->second) {
      if (!excepted.contains(selector)) {
        AppendRule(css, selector);
      }
    }
  }
  return css;
}

base::ListValue ExceptionSessionRules(const std::set<std::string>& sites) {
  base::ListValue rules;
  int id = kExceptionRuleId;
  for (const std::string& site : sites) {
    if (!IsSiteKey(site)) {
      continue;
    }
    if (id >= kExceptionRuleId + kMaxExceptionRules) {
      break;
    }
    // The URL's host is the site, with or without one www., then a port, the path or the end. RE2 syntax; every
    // character that is not a letter or digit is escaped, so dots and an IPv6 literal's brackets match only
    // themselves.
    std::string host;
    for (const char c : site) {
      if (!base::IsAsciiAlphaNumeric(c)) {
        host.push_back('\\');
      }
      host.push_back(c);
    }
    rules.Append(base::DictValue()
                     .Set("id", id++)
                     .Set("priority", kExceptionRulePriority)
                     .Set("action", base::DictValue().Set("type",
                                                          "allowAllRequests"))
                     .Set("condition",
                          base::DictValue()
                              .Set("regexFilter",
                                   base::StrCat({"^https?://(www\\.)?", host,
                                                 "(:[0-9]+)?([/?#]|$)"}))
                              .Set("resourceTypes",
                                   base::ListValue().Append("main_frame"))));
  }
  return rules;
}

}  // namespace clu
