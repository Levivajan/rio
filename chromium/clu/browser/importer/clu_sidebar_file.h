// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_SIDEBAR_FILE_H_
#define CLU_BROWSER_IMPORTER_CLU_SIDEBAR_FILE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "clu/browser/importer/clu_chrome_bookmarks.h"
#include "clu/browser/theme/clu_theme_math.h"

// The sidebar-file parser (docs/plan/07-data.md and R7, decision 56): another browser's sidebar file to a plan of
// source profiles, their favorites, and spaces with pinned trees. The file is only read. In the UI this importer is
// "Sidebar file from another browser".
//
// Layout read: `sidebar.containers[1]` holds `spaces`, `items` and `topAppsContainerIDs`, each a flat alternating
// array `[key, value, ...]` read in pairs (an odd tail is ignored). A space's `containerIDs` alternates
// `["pinned", id, "unpinned", id]`. Items link down through `childrenIds`.
//
// Mapping: one plan profile per source profile key (`default`, or `custom:<directoryBasename>`) that a space uses, in
// first-seen order (profile folders no space uses are left out). This machine is the `machineID` the first
// space's profile names; another machine's favorites containers are left out. Pinned `tab` items become pinned tabs (`title` is the user's rename, `savedTitle` the page title, else the
// host), `list` items become folders (collapsed when `isCollapsed: true` or `isExpanded: false`), and a split view's
// tabs become consecutive tabs in its place plus a split of them (`splits`). A profile's favorites are the tabs under its favorites container,
// and a space's Today tabs the tabs under its unpinned container (folders and split views there are flattened). The space emoji is kept when it is an emoji of at most 8 UTF-16 units;
// the space's window theme becomes a SpaceTheme (up to 3 colors from its background, then anywhere in it).
//
// Skip counts: `unsupported` items of any other kind (notes, welcome items) and
// unreadable spaces; `missing` child or container ids with no item; `cycles` ids already visited (a visited set
// breaks childrenIds cycles); `bad_urls` tabs whose URL isn't http(s); `over_cap` nodes past 10,000. Pure, iterative.
namespace clu {

inline constexpr char kSidebarUnsupported[] =
    "This sidebar file format isn't supported";
inline constexpr char kSidebarTooLarge[] =
    "This sidebar file is too large to import.";
inline constexpr char kSidebarUnreadable[] =
    "The sidebar file couldn’t be read.";

struct SidebarSkips {
  int unsupported = 0;
  int missing = 0;
  int cycles = 0;
  int bad_urls = 0;
  int over_cap = 0;
  // Counted when writing: favorites the profile already has, and favorites past the 12-favorite cap.
  int duplicates = 0;
  // Counted when writing: a source profile's History file that couldn't be read.
  int history_files = 0;

  int total() const {
    return unsupported + missing + cycles + bad_urls + over_cap + duplicates +
           history_files;
  }
};

// A split view's tabs in pane order, at least 2 (a split left with one readable tab is none).
struct SidebarSplitPlan {
  SidebarSplitPlan();
  SidebarSplitPlan(const SidebarSplitPlan&);
  SidebarSplitPlan& operator=(const SidebarSplitPlan&);
  SidebarSplitPlan(SidebarSplitPlan&&);
  SidebarSplitPlan& operator=(SidebarSplitPlan&&);
  ~SidebarSplitPlan();

  // Keys "item:<id>" of tabs in `pinned` or `today`.
  std::vector<std::string> keys;
  // `layoutOrientation: "vertical"`: the panes are stacked, else side by side.
  bool stacked = false;
};

struct SidebarSpacePlan {
  SidebarSpacePlan();
  SidebarSpacePlan(const SidebarSpacePlan&);
  SidebarSpacePlan& operator=(const SidebarSpacePlan&);
  SidebarSpacePlan(SidebarSpacePlan&&);
  SidebarSpacePlan& operator=(SidebarSpacePlan&&);
  ~SidebarSpacePlan();

  // "space:<id>", stable across runs.
  std::string key;
  std::string profile_key;
  // As the file has it, cleaned; empty gives Clu's default name.
  std::string name;
  std::optional<std::string> emoji;
  // Without an emoji, a symbol icon as the closest of SPACE_SYMBOLS (ClosestSpaceSymbol()).
  std::optional<std::string> symbol;
  std::optional<SpaceTheme> theme;
  // Preorder; keys "item:<id>".
  std::vector<ImportNode> pinned;
  // Tabs only, top to bottom; keys "item:<id>".
  std::vector<ImportNode> today;
  // The split views of `pinned` and `today`.
  std::vector<SidebarSplitPlan> splits;
};

struct SidebarProfilePlan {
  SidebarProfilePlan();
  SidebarProfilePlan(const SidebarProfilePlan&);
  SidebarProfilePlan& operator=(const SidebarProfilePlan&);
  SidebarProfilePlan(SidebarProfilePlan&&);
  SidebarProfilePlan& operator=(SidebarProfilePlan&&);
  ~SidebarProfilePlan();

  // "default" or "custom:<directoryBasename>".
  std::string key;
  // Tabs only, in order.
  std::vector<ImportNode> favorites;
  // The source profile's own name (Local State's profile.info_cache), set by the import service; empty when it has
  // none.
  std::string name;
};

struct SidebarPlan {
  SidebarPlan();
  SidebarPlan(const SidebarPlan&) = delete;
  SidebarPlan& operator=(const SidebarPlan&) = delete;
  SidebarPlan(SidebarPlan&&);
  SidebarPlan& operator=(SidebarPlan&&);
  ~SidebarPlan();

  // Set when the file is not a supported sidebar file; nothing else is then.
  std::optional<std::string> error;
  std::vector<SidebarProfilePlan> profiles;
  std::vector<SidebarSpacePlan> spaces;
  SidebarSkips skipped;
};

SidebarPlan ParseSidebarFile(std::string_view text);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_SIDEBAR_FILE_H_
