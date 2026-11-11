// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_live_feed.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "url/url_constants.h"

namespace clu {

namespace {

constexpr size_t kMaxUrlLength = 2048;

// A Gmail account selector, as /u/ or authuser= carries it: an index of 1 or 2 digits, or an email address (its "@"
// may come escaped), lowercased. Nullopt for anything else.
std::optional<std::string> GmailAccount(std::string_view raw) {
  if (raw.empty() || raw.size() > 254 * 3) {
    return std::nullopt;
  }
  if (raw.size() <= 2 && std::ranges::all_of(raw, base::IsAsciiDigit<char>)) {
    return std::string(raw);
  }
  std::string email;
  email = base::ToLowerASCII(raw);
  base::ReplaceSubstringsAfterOffset(&email, 0, "%40", "@");
  const size_t at = email.find('@');
  if (email.size() > 254 || at == 0 || at == std::string::npos ||
      at != email.rfind('@') || at + 1 == email.size()) {
    return std::nullopt;
  }
  for (size_t i = 0; i < email.size(); ++i) {
    const char c = email[i];
    const bool ok = base::IsAsciiAlphaNumeric(c) || c == '.' || c == '-' ||
                    (i < at && (c == '_' || c == '+')) || i == at;
    if (!ok) {
      return std::nullopt;
    }
  }
  return email;
}
constexpr size_t kMaxRepoPart = 100;

std::string_view LocalName(std::string_view tag) {
  const size_t colon = tag.rfind(':');
  return colon == std::string_view::npos ? tag : tag.substr(colon + 1);
}

bool IsElement(const base::Value& node, std::string_view local) {
  const base::DictValue* dict = node.GetIfDict();
  const std::string* type = dict ? dict->FindString("type") : nullptr;
  const std::string* tag = dict ? dict->FindString("tag") : nullptr;
  return type && *type == "element" && tag && LocalName(*tag) == local;
}

const base::ListValue* ChildrenOf(const base::Value& node) {
  const base::DictValue* dict = node.GetIfDict();
  return dict ? dict->FindList("children") : nullptr;
}

std::vector<const base::Value*> Children(const base::Value& node,
                                         std::string_view local) {
  std::vector<const base::Value*> out;
  if (const base::ListValue* children = ChildrenOf(node)) {
    for (const base::Value& child : *children) {
      if (IsElement(child, local)) {
        out.push_back(&child);
      }
    }
  }
  return out;
}

const base::Value* Child(const base::Value& node, std::string_view local) {
  std::vector<const base::Value*> all = Children(node, local);
  return all.empty() ? nullptr : all.front();
}

std::string Attribute(const base::Value& node, std::string_view name) {
  const base::DictValue* attributes = node.GetDict().FindDict("attributes");
  const std::string* value = attributes ? attributes->FindString(name) : nullptr;
  return value ? *value : std::string();
}

// Text and CDATA under an element, whitespace collapsed, cut to kMaxLiveTitleLength characters.
std::string TextOf(const base::Value* node) {
  std::string text;
  if (const base::ListValue* children = node ? ChildrenOf(*node) : nullptr) {
    for (const base::Value& child : *children) {
      const base::DictValue* dict = child.GetIfDict();
      const std::string* type = dict ? dict->FindString("type") : nullptr;
      const std::string* value = dict ? dict->FindString("text") : nullptr;
      if (type && value && (*type == "text" || *type == "cdata")) {
        text += *value;
      }
    }
  }
  std::u16string wide = base::CollapseWhitespace(base::UTF8ToUTF16(text),
                                                 /*trim_sequences_with_line_breaks=*/false);
  if (wide.size() > kMaxLiveTitleLength) {
    wide = wide.substr(0, kMaxLiveTitleLength);
  }
  return base::UTF16ToUTF8(wide);
}

std::optional<std::string> WebUrl(std::string_view text, const GURL& base) {
  const std::string trimmed(base::TrimWhitespaceASCII(text, base::TRIM_ALL));
  if (trimmed.empty() || trimmed.size() > kMaxUrlLength) {
    return std::nullopt;
  }
  const GURL url = base.is_valid() ? base.Resolve(trimmed) : GURL(trimmed);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host() ||
      url.spec().size() > kMaxUrlLength) {
    return std::nullopt;
  }
  return url.spec();
}

// Atom: the link with rel="alternate" or no rel.
std::optional<std::string> AtomLink(const base::Value& entry,
                                    const GURL& base) {
  for (const base::Value* link : Children(entry, "link")) {
    const std::string rel = Attribute(*link, "rel");
    if (rel.empty() || rel == "alternate") {
      if (std::optional<std::string> url = WebUrl(Attribute(*link, "href"), base)) {
        return url;
      }
    }
  }
  return std::nullopt;
}

// RSS: <link>, else a permalink <guid>.
std::optional<std::string> RssLink(const base::Value& item, const GURL& base) {
  if (std::optional<std::string> url = WebUrl(TextOf(Child(item, "link")), base)) {
    return url;
  }
  const base::Value* guid = Child(item, "guid");
  if (guid && Attribute(*guid, "isPermaLink") != "false") {
    return WebUrl(TextOf(guid), base);
  }
  return std::nullopt;
}

bool IsRepoPart(std::string_view part) {
  return !part.empty() && part.size() <= kMaxRepoPart && part != "." &&
         part != ".." && std::ranges::all_of(part, [](char c) {
           return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_' ||
                  c == '.';
         });
}

// "owner/repo" from "owner/repo", "github.com/owner/repo" or "https://github.com/owner/repo/…".
std::optional<std::string> RepoOf(std::string_view source) {
  std::string_view s = base::TrimWhitespaceASCII(source, base::TRIM_ALL);
  for (std::string_view prefix : {"https://", "http://"}) {
    if (base::StartsWith(s, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
      s.remove_prefix(prefix.size());
    }
  }
  for (std::string_view prefix : {"www.github.com/", "github.com/"}) {
    if (base::StartsWith(s, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
      s.remove_prefix(prefix.size());
    }
  }
  const std::vector<std::string_view> parts = base::SplitStringPiece(
      s, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (parts.size() < 2 || !IsRepoPart(parts[0]) || !IsRepoPart(parts[1])) {
    return std::nullopt;
  }
  std::string_view repo = parts[1];
  if (base::EndsWith(repo, ".git")) {
    repo.remove_suffix(4);
  }
  if (!IsRepoPart(repo)) {
    return std::nullopt;
  }
  return base::StrCat({parts[0], "/", repo});
}

}  // namespace

LiveFeed::LiveFeed() = default;
LiveFeed::LiveFeed(const LiveFeed&) = default;
LiveFeed& LiveFeed::operator=(const LiveFeed&) = default;
LiveFeed::~LiveFeed() = default;

LiveFolderPlan::LiveFolderPlan() = default;
LiveFolderPlan::LiveFolderPlan(const LiveFolderPlan&) = default;
LiveFolderPlan& LiveFolderPlan::operator=(const LiveFolderPlan&) = default;
LiveFolderPlan::~LiveFolderPlan() = default;

std::optional<LiveFeed> ParseLiveFeed(const base::Value& root,
                                      const GURL& base) {
  if (!root.is_dict()) {
    return std::nullopt;
  }
  LiveFeed feed;
  std::set<std::string> seen;
  auto add = [&](std::optional<std::string> url, std::string title) {
    if (!url || feed.items.size() >= kMaxLiveFolderItems ||
        !seen.insert(*url).second) {
      return;
    }
    feed.items.push_back({*url, title.empty() ? *url : std::move(title)});
  };
  if (IsElement(root, "feed")) {
    feed.title = TextOf(Child(root, "title"));
    for (const base::Value* entry : Children(root, "entry")) {
      add(AtomLink(*entry, base), TextOf(Child(*entry, "title")));
    }
    return feed;
  }
  const base::Value* channel = nullptr;
  std::vector<const base::Value*> items;
  if (IsElement(root, "rss")) {
    channel = Child(root, "channel");
    if (!channel) {
      return std::nullopt;
    }
    items = Children(*channel, "item");
  } else if (IsElement(root, "RDF")) {
    // RSS 1.0 lists its items beside the channel.
    channel = Child(root, "channel");
    items = Children(root, "item");
  } else {
    return std::nullopt;
  }
  feed.title = TextOf(channel ? Child(*channel, "title") : nullptr);
  for (const base::Value* item : items) {
    add(RssLink(*item, base), TextOf(Child(*item, "title")));
  }
  return feed;
}

std::optional<GURL> LiveFolderFeedUrl(std::string_view kind,
                                      std::string_view source) {
  if (source.size() > kMaxUrlLength) {
    return std::nullopt;
  }
  if (kind == kLiveFeed) {
    std::string text(base::TrimWhitespaceASCII(source, base::TRIM_ALL));
    if (text.find("://") == std::string::npos) {
      text = base::StrCat({"https://", text});
    }
    const GURL url(text);
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host() ||
        url.host().find('.') == std::string::npos || url.has_username() ||
        url.has_password()) {
      return std::nullopt;
    }
    return url.GetWithoutRef();
  }
  const std::optional<std::string> repo = RepoOf(source);
  if (!repo) {
    return std::nullopt;
  }
  if (kind == kLiveGithubReleases) {
    return GURL(base::StrCat({"https://github.com/", *repo, "/releases.atom"}));
  }
  if (kind == kLiveGithubCommits) {
    return GURL(base::StrCat({"https://github.com/", *repo, "/commits.atom"}));
  }
  return std::nullopt;
}

std::string LiveFolderName(std::string_view kind, const GURL& feed_url) {
  if (kind == kLiveGithubReleases || kind == kLiveGithubCommits) {
    const std::vector<std::string_view> parts = base::SplitStringPiece(
        feed_url.path(), "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (parts.size() >= 2) {
      return base::StrCat({parts[0], "/", parts[1],
                           kind == kLiveGithubReleases ? " Releases"
                                                       : " Commits"});
    }
  }
  std::string host(feed_url.host());
  if (base::StartsWith(host, "www.")) {
    host.erase(0, 4);
  }
  return host;
}

LiveFolderPlan PlanLiveFolder(const std::vector<LiveRow>& rows,
                              const std::vector<LiveFeedItem>& items) {
  LiveFolderPlan plan;
  std::set<std::string> listed;
  for (const LiveFeedItem& item : items) {
    listed.insert(item.url);
  }
  std::set<std::string> kept;
  for (const LiveRow& row : rows) {
    if (listed.contains(row.url) || row.open) {
      kept.insert(row.url);
    } else {
      plan.remove.push_back(row.id);
    }
  }
  int index = 0;
  for (const LiveFeedItem& item : items) {
    if (!kept.contains(item.url)) {
      plan.insert.emplace_back(index++, item);
      kept.insert(item.url);
    }
  }
  return plan;
}

GmailUnread::GmailUnread() = default;
GmailUnread::GmailUnread(const GmailUnread&) = default;
GmailUnread& GmailUnread::operator=(const GmailUnread&) = default;
GmailUnread::~GmailUnread() = default;

std::optional<GmailUnread> ParseGmailFeed(const base::Value& root) {
  if (!IsElement(root, "feed")) {
    return std::nullopt;
  }
  const base::Value* fullcount = Child(root, "fullcount");
  int count = 0;
  if (!fullcount || !base::StringToInt(TextOf(fullcount), &count) ||
      count < 0) {
    return std::nullopt;
  }
  GmailUnread unread;
  const GURL mail("https://mail.google.com/mail/");
  for (const base::Value* entry : Children(root, "entry")) {
    GmailUnreadItem item;
    item.title = TextOf(Child(*entry, "title"));
    if (const base::Value* author = Child(*entry, "author")) {
      item.author = TextOf(Child(*author, "name"));
      if (item.author.empty()) {
        item.author = TextOf(Child(*author, "email"));
      }
    }
    item.issued = TextOf(Child(*entry, "issued"));
    if (item.issued.empty()) {
      item.issued = TextOf(Child(*entry, "modified"));
    }
    if (item.issued.size() > 40) {
      item.issued.clear();
    }
    // Only a link to the message on Gmail itself; anything else opens nothing.
    if (std::optional<std::string> link = AtomLink(*entry, mail)) {
      const GURL url(*link);
      if (url.SchemeIs(url::kHttpsScheme) && url.host() == "mail.google.com") {
        item.url = *link;
      }
    }
    unread.items.push_back(std::move(item));
  }
  // Newest first: the feed's times are all UTC in one format, so they order as text; an entry without one goes last.
  std::ranges::stable_sort(unread.items, [](const GmailUnreadItem& a,
                                            const GmailUnreadItem& b) {
    return a.issued > b.issued;
  });
  const int listed = static_cast<int>(unread.items.size());
  if (unread.items.size() > kMaxGmailUnreadItems) {
    unread.items.resize(kMaxGmailUnreadItems);
  }
  unread.count = std::min(std::max(count, listed), 999999);
  return unread;
}

std::optional<GURL> GmailFeedUrl(std::string_view page_url) {
  const GURL url(page_url.substr(0, kMaxUrlLength));
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
      url.host() != "mail.google.com") {
    return std::nullopt;
  }
  // The account Gmail addresses: the path's /u/ segment ("/mail/u/1/", "/mail/u/me@x.com/"), else ?authuser=. With
  // none it is account 0. A selector that is neither an index nor an address, or two that disagree, names no account
  // this can read: no feed, so the card never shows another account's mail.
  std::optional<std::string> from_path;
  const std::string_view path = url.path();
  if (const size_t at = path.find("/u/"); at != std::string_view::npos) {
    std::string_view rest = path.substr(at + 3);
    rest = rest.substr(0, rest.find('/'));
    from_path = GmailAccount(rest);
    if (!from_path) {
      return std::nullopt;
    }
  }
  std::optional<std::string> from_query;
  for (std::string_view pair : base::SplitStringPiece(
           url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (pair.starts_with("authuser=")) {
      from_query = GmailAccount(pair.substr(9));
      if (!from_query) {
        return std::nullopt;
      }
    }
  }
  if (from_path && from_query && *from_path != *from_query) {
    return std::nullopt;
  }
  const std::string account = from_path.value_or(from_query.value_or("0"));
  return GURL(
      base::StrCat({"https://mail.google.com/mail/u/", account, "/feed/atom"}));
}

}  // namespace clu
