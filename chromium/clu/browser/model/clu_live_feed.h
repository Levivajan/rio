// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_LIVE_FEED_H_
#define CLU_BROWSER_MODEL_CLU_LIVE_FEED_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"
#include "url/gurl.h"

// Live Folders (the space menu's Live Folders ▸): a pinned folder that fills itself from a public feed and keeps up
// with it. It reads only public data that needs no sign-in: any RSS or Atom feed, and a public GitHub repository's
// releases or commits (GitHub's own Atom feeds). Pure, so clu_unittests covers it; CluLiveFolders fetches and applies
// it.
namespace clu {

// The newest entries a live folder keeps.
inline constexpr size_t kMaxLiveFolderItems = 20;
// Longer titles are cut.
inline constexpr size_t kMaxLiveTitleLength = 300;

// What the folder follows (folders.createLive `kind`).
inline constexpr char kLiveFeed[] = "feed";
inline constexpr char kLiveGithubReleases[] = "githubReleases";
inline constexpr char kLiveGithubCommits[] = "githubCommits";

struct LiveFeedItem {
  std::string url;
  std::string title;
  bool operator==(const LiveFeedItem&) const = default;
};

struct LiveFeed {
  LiveFeed();
  LiveFeed(const LiveFeed&);
  LiveFeed& operator=(const LiveFeed&);
  ~LiveFeed();

  std::string title;
  std::vector<LiveFeedItem> items;
};

// An RSS 2.0, RSS 1.0 (RDF) or Atom document as the data decoder's XML parser returns it: its title and its first
// kMaxLiveFolderItems entries with an http or https link (relative links resolve against `base`), each URL once.
// Nullopt for anything that is no feed.
std::optional<LiveFeed> ParseLiveFeed(const base::Value& root, const GURL& base);

// The feed a source names: an http or https URL for kLiveFeed ("example.com/feed" gets https://), "owner/repo" or a
// github.com repository URL for the GitHub kinds. Nullopt for a junk source or an unknown kind.
std::optional<GURL> LiveFolderFeedUrl(std::string_view kind,
                                      std::string_view source);

// The folder's name until its feed answers with a title: "owner/repo Releases" or "owner/repo Commits", else the
// feed's host.
std::string LiveFolderName(std::string_view kind, const GURL& feed_url);

// A row of the folder as it is: its URL, and whether a page shows it (an open row is never taken away).
struct LiveRow {
  std::string id;
  std::string url;
  bool open = false;
};

struct LiveFolderPlan {
  LiveFolderPlan();
  LiveFolderPlan(const LiveFolderPlan&);
  LiveFolderPlan& operator=(const LiveFolderPlan&);
  ~LiveFolderPlan();

  // Rows the feed no longer lists, unless open.
  std::vector<std::string> remove;
  // New entries, newest first, each with its index in the folder once `remove` is gone (they go to the top).
  std::vector<std::pair<int, LiveFeedItem>> insert;
};

// The unread mail of one Gmail account, from Gmail's Atom feed (https://mail.google.com/mail/u/N/feed/atom:
// <fullcount> and an <entry> per unread message with <title>, <author><name>, <issued> and a link to the message).
// The favorite's and pinned row's mail card lists the newest kMaxGmailUnreadItems.
inline constexpr size_t kMaxGmailUnreadItems = 5;

struct GmailUnreadItem {
  std::string author;
  std::string title;
  // ISO 8601, as the feed gives it ("2026-11-10T19:59:00Z"); the card shows it as a time.
  std::string issued;
  // The message on mail.google.com, empty when the feed gave no such link.
  std::string url;
  bool operator==(const GmailUnreadItem&) const = default;
};

struct GmailUnread {
  GmailUnread();
  GmailUnread(const GmailUnread&);
  GmailUnread& operator=(const GmailUnread&);
  ~GmailUnread();

  int count = 0;
  // Newest first.
  std::vector<GmailUnreadItem> items;
};

// The feed as the data decoder's XML parser returns it. Nullopt for anything without a <feed> root and a numeric
// <fullcount> (a sign-in page, an error page, junk). The count is never below the entries listed.
std::optional<GmailUnread> ParseGmailFeed(const base::Value& root);

// The Atom feed of the account a Gmail page is signed in as: its /mail/u/<index or address>/, else its ?authuser=,
// else account 0. Nullopt for any other page, and for a Gmail page whose account selector cannot be read.
std::optional<GURL> GmailFeedUrl(std::string_view page_url);

LiveFolderPlan PlanLiveFolder(const std::vector<LiveRow>& rows,
                              const std::vector<LiveFeedItem>& items);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_LIVE_FEED_H_
