// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_SITE_CLU_BLOCKER_RULES_H_
#define CLU_BROWSER_SITE_CLU_BLOCKER_RULES_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

// The blocker's data as values (docs/chromium/m7.md): the declarativeNetRequest rules and hiding
// selectors scripts/chromium/blocker compiled from the filter lists, read back defensively, and the session rule that
// carries the profile's per-site exceptions. Pure, so clu_unittests covers it without an extension system.
namespace clu {

// The resources' version: 16 lowercase hex digits.
bool IsBlockerVersion(std::string_view version);

struct BlockerRules {
  BlockerRules();
  BlockerRules(BlockerRules&&);
  BlockerRules& operator=(BlockerRules&&);
  ~BlockerRules();

  std::string version;
  // DNR rule dicts with ids 1..kMaxBlockerRules, as the extension API's Rule::FromValue() reads them.
  base::ListValue rules;
  // Entries that were not rule dicts with a usable id; dropped.
  int skipped = 0;
};

// Chromium's dynamic rule limit for one extension (MAX_NUMBER_OF_DYNAMIC_RULES).
inline constexpr int kMaxBlockerRules = 30000;

// {version, rules: [...]} from IDR_CLU_OVERLAY_PAGE_BLOCKER_RULES_JSON (or the test fixture). Null for junk.
std::optional<BlockerRules> ParseBlockerRules(std::string_view json);

struct CosmeticData {
  CosmeticData();
  CosmeticData(const CosmeticData&);
  CosmeticData& operator=(const CosmeticData&);
  CosmeticData(CosmeticData&&);
  CosmeticData& operator=(CosmeticData&&);
  ~CosmeticData();

  std::string version;
  std::vector<std::string> generic;
  std::map<std::string, std::vector<std::string>, std::less<>> domains;
  std::map<std::string, std::set<std::string>, std::less<>> exceptions;
  std::set<std::string, std::less<>> generic_hide;
  // The generic selectors as CSS, built once.
  std::string generic_css;
};

// {version, generic, domains, exceptions, genericHide} from IDR_CLU_OVERLAY_PAGE_BLOCKER_COSMETIC_JSON. Selectors
// that could leave their rule and hosts that are not hosts are dropped. Null for junk.
std::optional<CosmeticData> ParseCosmeticData(std::string_view json);

// The user-origin CSS that hides ads on a page of `host`: the generic selectors (unless the host or a parent domain
// turns generic hiding off or excepts one), then the selectors of the host and its parent domains. One rule per
// selector, so one selector Blink does not know never drops the others.
std::string CosmeticCssFor(const CosmeticData& data, std::string_view host);

// The session rule ids of the exceptions: kExceptionRuleId up to kExceptionRuleId + kMaxExceptionRules - 1.
inline constexpr int kExceptionRuleId = 1;
// One regex rule per site, and Chromium allows 1,000 regex rules across an extension's dynamic and session rules (the
// compiled lists have none). Sites past the cap stay blocked.
inline constexpr int kMaxExceptionRules = 1000;
// Above every compiled rule (blocks are 1, list exceptions 2).
inline constexpr int kExceptionRulePriority = 100;

// The session rules for the profile's exceptions: per excepted site, one allowAllRequests rule for a top-level page
// whose host is exactly the site or www. before it, which covers that page's frames and requests. Subdomains and the
// site's frames inside other pages stay blocked, as CluSiteService::IsBlockingEnabled says. None when there is no
// exception.
base::ListValue ExceptionSessionRules(const std::set<std::string>& sites);

}  // namespace clu

#endif  // CLU_BROWSER_SITE_CLU_BLOCKER_RULES_H_
