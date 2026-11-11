// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_TIDY_H_
#define CLU_BROWSER_MODEL_CLU_TIDY_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class GURL;

// The app settings AI tab's tidy rules (docs/chromium/parity-command.md): shorter titles for pinned tabs and
// clearer names for downloads. Apple's on-device model (rio-ai, clu_ai_client.h) answers first where the Mac has
// one; these rules decide what needs tidying and answer when it cannot. Pure, so clu_unittests covers them.
namespace clu {

// The longest tidy title, in UTF-16 units; longer ones are cut at a word.
inline constexpr size_t kMaxTidyTitle = 40;
// The longest tidy download name before its extension, in UTF-16 units.
inline constexpr size_t kMaxTidyDownloadStem = 60;

// Tidy Tab Titles (ai.tidyTitles): the title a tab gets when it is pinned, or nullopt to keep its own. Drops an
// unread count ("(3) Inbox"), the site's name among parts split by " - ", " | ", " · " and the like ("Interior Design
// Book - Google Docs" on docs.google.com is "Interior Design Book"), keeps the first part left, and cuts it to
// kMaxTidyTitle at a word. A title that is only the site's name, an address, or already tidy gives nullopt.
std::optional<std::string> TidyPinnedTitle(std::string_view title,
                                           const GURL& url);

// Tidy Downloads (ai.tidyDownloads): a clearer name for a finished download, or nullopt to keep its own. Only junk
// names change: digits, hex or ids ("387494876.pdf", "a3f9c2e1-....pdf"), and "download", "file", "document" and the
// like, with or without a copy number ("file (2).pdf"). The new name is the page's tidy title (or its title) with
// characters macOS or Chromium refuse in a file name replaced, plus the old extension.
std::optional<std::string> TidyDownloadName(std::string_view file_name,
                                            std::string_view page_title,
                                            const GURL& page_url);

// Tidy Today (ai.tidyTabs) with the model: rio-ai answers a topic per tab as "1: Rio | 2: Python | 3: Rio", tab
// numbers from 1 to `count`. Tabs with the same topic (any case) form a group. Returns the tab indexes (0-based) in
// tidy order: the biggest group first (ties keep the order topics first appear in), each group in the tabs' own order,
// a tab the answer left out at the end in its own place. Nullopt for an answer that is not that shape (an item without
// a number and a topic, a number out of range or twice, fewer than two topics, or no topic shared by two tabs).
std::optional<std::vector<int>> TidyOrderFromModel(std::string_view answer,
                                                   int count);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_TIDY_H_
