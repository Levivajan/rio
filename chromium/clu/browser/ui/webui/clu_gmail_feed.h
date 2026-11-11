// Copyright 2026 Levon Adamyan. SPDX-License-Identifier: GPL-3.0-only

#ifndef CLU_BROWSER_UI_WEBUI_CLU_GMAIL_FEED_H_
#define CLU_BROWSER_UI_WEBUI_CLU_GMAIL_FEED_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "clu/browser/model/clu_live_feed.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace network {
class SimpleURLLoader;
}

namespace clu {

// A Gmail favorite's or pinned row's unread mail, read from Gmail's Atom feed for the row's account
// (GmailFeedUrl) with the row's own profile and cookies, without loading any Gmail page. The window's hover card
// asks for it and waits for the answer, so the card never shows a guess first. One fetch per feed at a time; an
// answer is kept and refreshed when it is older than kGmailFeedFresh.
class CluGmailFeed {
 public:
  struct Result {
    // False when the feed did not answer with a Gmail feed (offline, signed out, an error page, a timeout).
    bool ok = false;
    GmailUnread unread;
    base::TimeTicks at;
  };

  CluGmailFeed();
  CluGmailFeed(const CluGmailFeed&) = delete;
  CluGmailFeed& operator=(const CluGmailFeed&) = delete;
  ~CluGmailFeed();

  // The last answer for `feed` read in `context`, or null before the first.
  const Result* Find(content::BrowserContext* context, const GURL& feed) const;
  // Whether that answer is older than kGmailFeedFresh (or missing), so a new fetch is due.
  bool Stale(content::BrowserContext* context, const GURL& feed) const;
  // Reads `feed` in `context` unless a read is running; `done` runs once it has an answer, ok or not.
  void Fetch(content::BrowserContext* context,
             const GURL& feed,
             base::OnceClosure done);

  // Tests: every feed is read from this origin (the test server) instead of mail.google.com, the path kept.
  static void SetFeedOriginForTesting(const GURL& origin);

 private:
  using Key = std::pair<const content::BrowserContext*, std::string>;

  void OnFetched(Key key,
                 network::SimpleURLLoader* loader,
                 std::optional<std::string> body);
  void OnParsed(Key key, base::expected<base::Value, std::string> parsed);
  void Finish(const Key& key, std::optional<GmailUnread> unread);

  std::map<Key, Result> results_;
  std::map<Key, std::vector<base::OnceClosure>> waiting_;
  std::set<std::unique_ptr<network::SimpleURLLoader>, base::UniquePtrComparator>
      loaders_;
  base::WeakPtrFactory<CluGmailFeed> weak_ptr_factory_{this};
};

// A feed answer is reused this long; after it the next hover reads the feed again.
inline constexpr base::TimeDelta kGmailFeedFresh = base::Seconds(60);
// A feed that has not answered by then is no answer: the card falls back to the tab title's count.
inline constexpr base::TimeDelta kGmailFeedTimeout = base::Seconds(4);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_GMAIL_FEED_H_
