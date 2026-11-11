// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_blocker_rules.h"

#include <set>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "clu/browser/site/clu_site_extension.h"
#include "components/crx_file/id_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/re2/src/re2/re2.h"

// The blocker's data read back (docs/chromium/m7.md): the compiled rules and selectors, junk dropped, the hiding CSS
// per host, and the exceptions' session rule.
namespace clu {
namespace {

constexpr char kVersion[] = "0123456789abcdef";

std::string ReadFixture(const char* name) {
  base::FilePath root;
  EXPECT_TRUE(base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &root));
  std::string text;
  EXPECT_TRUE(base::ReadFileToString(root.AppendASCII("clu")
                                         .AppendASCII("test")
                                         .AppendASCII("data")
                                         .AppendASCII("blocker")
                                         .AppendASCII(name),
                                     &text));
  return text;
}

TEST(CluSiteExtensionTest, TheIdIsTheKeys) {
  std::string der;
  ASSERT_TRUE(base::Base64Decode(kCluExtensionPublicKey, &der));
  EXPECT_EQ(crx_file::id_util::GenerateId(der), kCluExtensionId);
  EXPECT_NE(std::string(kCluExtensionManifest).find(kCluExtensionPublicKey),
            std::string::npos);
  EXPECT_TRUE(base::JSONReader::ReadDict(kCluExtensionManifest,
                                         base::JSON_PARSE_RFC));
}

TEST(CluBlockerRulesTest, TheFixtureReadsBack) {
  const std::optional<BlockerRules> rules =
      ParseBlockerRules(ReadFixture("rules.json"));
  ASSERT_TRUE(rules);
  EXPECT_TRUE(IsBlockerVersion(rules->version));
  ASSERT_EQ(rules->rules.size(), 2u);
  EXPECT_EQ(rules->skipped, 0);
  const std::optional<CosmeticData> cosmetic =
      ParseCosmeticData(ReadFixture("cosmetic.json"));
  ASSERT_TRUE(cosmetic);
  EXPECT_EQ(cosmetic->version, rules->version);
  EXPECT_EQ(CosmeticCssFor(*cosmetic, "127.0.0.1"),
            ".ad-slot{display:none!important}\n"
            ".host-slot{display:none!important}\n");
  EXPECT_EQ(CosmeticCssFor(*cosmetic, "tracker.localhost"),
            ".ad-slot{display:none!important}\n"
            ".host-slot{display:none!important}\n");
  EXPECT_EQ(CosmeticCssFor(*cosmetic, "a.test"),
            ".ad-slot{display:none!important}\n");
}

TEST(CluBlockerRulesTest, JunkRulesAndFilesAreDropped) {
  EXPECT_FALSE(ParseBlockerRules("not json"));
  EXPECT_FALSE(ParseBlockerRules(R"({"version":"x","rules":[]})"));
  EXPECT_FALSE(ParseBlockerRules(R"({"version":"0123456789abcdef"})"));
  const std::optional<BlockerRules> rules = ParseBlockerRules(
      R"({"version":"0123456789abcdef","rules":[
        {"id":1,"action":{"type":"block"},"condition":{"urlFilter":"/a"}},
        {"id":1,"action":{"type":"block"},"condition":{"urlFilter":"/dup"}},
        {"id":0,"action":{},"condition":{}},
        {"id":30001,"action":{},"condition":{}},
        {"id":"2","action":{},"condition":{}},
        {"id":3,"condition":{}},
        7]})");
  ASSERT_TRUE(rules);
  EXPECT_EQ(rules->rules.size(), 1u);
  EXPECT_EQ(rules->skipped, 6);
}

TEST(CluBlockerRulesTest, HidingHonorsExceptionsGenericHideAndParentDomains) {
  const std::optional<CosmeticData> data = ParseCosmeticData(base::StrCat(
      {R"({"version":")", kVersion, R"(",
         "generic":[".ad", ".banner", "a{b}", 5],
         "domains":{"news.test":[".promo"], "BAD HOST":[".x"], "deep.news.test":[".deep"]},
         "exceptions":{"news.test":[".banner"]},
         "genericHide":["quiet.test", "not a host"]})"}));
  ASSERT_TRUE(data);
  EXPECT_EQ(data->generic.size(), 2u);
  EXPECT_EQ(data->domains.size(), 2u);
  EXPECT_EQ(CosmeticCssFor(*data, "other.test"),
            ".ad{display:none!important}\n.banner{display:none!important}\n");
  EXPECT_EQ(CosmeticCssFor(*data, "deep.news.test"),
            ".ad{display:none!important}\n.deep{display:none!important}\n"
            ".promo{display:none!important}\n");
  EXPECT_EQ(CosmeticCssFor(*data, "sub.quiet.test"), "");
  EXPECT_FALSE(ParseCosmeticData(R"({"generic":[]})"));
}

// One rule per excepted site, matching a top-level page on exactly that site (or its www.): turning blocking off on
// youtube.com leaves its embeds on other pages and music.youtube.com blocked.
TEST(CluBlockerRulesTest, ExceptionsAllowOnlyTheExactTopLevelSite) {
  EXPECT_TRUE(ExceptionSessionRules({}).empty());
  const base::ListValue rules = ExceptionSessionRules(
      {"a.test", "127.0.0.1", "www.bad.test", "[::1]", "Junk Site"});
  ASSERT_EQ(rules.size(), 3u);
  std::vector<std::string> filters;
  for (int i = 0; i < 3; ++i) {
    const base::DictValue& rule = rules[i].GetDict();
    EXPECT_EQ(rule.FindInt("id"), kExceptionRuleId + i);
    EXPECT_EQ(rule.FindInt("priority"), kExceptionRulePriority);
    EXPECT_EQ(*rule.FindStringByDottedPath("action.type"), "allowAllRequests");
    EXPECT_FALSE(rule.FindByDottedPath("condition.requestDomains"));
    EXPECT_EQ(*rule.FindListByDottedPath("condition.resourceTypes"),
              base::ListValue().Append("main_frame"));
    filters.push_back(*rule.FindStringByDottedPath("condition.regexFilter"));
  }
  // std::set order: "127.0.0.1" < "[::1]" < "a.test".
  const RE2 ip(filters[0]);
  const RE2 v6(filters[1]);
  const RE2 site(filters[2]);
  ASSERT_TRUE(ip.ok() && v6.ok() && site.ok());
  EXPECT_TRUE(RE2::PartialMatch("http://127.0.0.1:8000/a", ip));
  EXPECT_FALSE(RE2::PartialMatch("http://127.0.0.10/", ip));
  EXPECT_FALSE(RE2::PartialMatch("http://127a0a0a1/", ip));
  EXPECT_TRUE(RE2::PartialMatch("http://[::1]:9/", v6));
  EXPECT_TRUE(RE2::PartialMatch("https://a.test/", site));
  EXPECT_TRUE(RE2::PartialMatch("https://www.a.test/watch?v=1", site));
  EXPECT_TRUE(RE2::PartialMatch("https://a.test", site));
  EXPECT_TRUE(RE2::PartialMatch("https://a.test?q", site));
  EXPECT_FALSE(RE2::PartialMatch("https://mail.a.test/", site));
  EXPECT_FALSE(RE2::PartialMatch("https://a.test.evil.test/", site));
  EXPECT_FALSE(RE2::PartialMatch("https://evil.test/?u=https://a.test/", site));
}

TEST(CluBlockerRulesTest, ExceptionsStopAtTheRegexRuleCap) {
  std::set<std::string> sites;
  for (int i = 0; i < kMaxExceptionRules + 5; ++i) {
    sites.insert(base::StrCat({"s", base::NumberToString(i), ".test"}));
  }
  EXPECT_EQ(ExceptionSessionRules(sites).size(),
            static_cast<size_t>(kMaxExceptionRules));
}

}  // namespace
}  // namespace clu
