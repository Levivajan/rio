// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_IMPORTER_CLU_CHROME_BOOKMARKS_H_
#define CLU_BROWSER_IMPORTER_CLU_CHROME_BOOKMARKS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The Chrome Bookmarks parser (docs/plan/07-data.md and R8): a profile's Bookmarks JSON to one "Imported from
// Chrome" folder with "Bookmarks bar", "Other bookmarks" and "Mobile bookmarks" inside and the original tree below
// them. Only http and https bookmarks are kept; the rest are skipped and counted. An empty title becomes the host.
// Folders nested deeper than 8 levels (counting the top folder) are flattened into their depth-8 ancestor. At most
// 10,000 items per run. Pure, so clu_unittests runs it on the fixture; it never throws or recurses.
namespace clu {

inline constexpr char kChromeImportFolderTitle[] = "Imported from Google Chrome";
inline constexpr char kUntitledFolderTitle[] = "Untitled folder";

// One node of an imported tree, in preorder: a folder comes before its children. `key` is stable across runs (the
// Chrome node's guid, else its id, else its path), scoped by the source profile directory, so a re-import finds it.
struct ImportNode {
  ImportNode();
  ImportNode(const ImportNode&);
  ImportNode& operator=(const ImportNode&);
  ImportNode(ImportNode&&);
  ImportNode& operator=(ImportNode&&);
  ~ImportNode();

  std::string key;
  // Empty at the root of the imported tree.
  std::string parent_key;
  bool folder = false;
  // Tabs only.
  std::string url;
  std::string title;
  // A user rename (sidebar files only).
  std::optional<std::string> title_override;
  // Folders only (sidebar files keep the source's collapsed state).
  bool collapsed = false;
};

struct BookmarksResult {
  BookmarksResult();
  BookmarksResult(const BookmarksResult&) = delete;
  BookmarksResult& operator=(const BookmarksResult&) = delete;
  BookmarksResult(BookmarksResult&&);
  BookmarksResult& operator=(BookmarksResult&&);
  ~BookmarksResult();

  // Set when the file is damaged (not JSON, no roots, nested deeper than the reader allows); nothing else is then.
  std::optional<std::string> error;
  // Preorder, starting with the "Imported from Chrome" folder; empty when the file holds nothing to import.
  std::vector<ImportNode> nodes;
  int bookmarks = 0;
  // Folders made from Chrome folders (the top folder and the three root folders not counted).
  int folders = 0;
  int skipped = 0;
};

inline constexpr char kBookmarksDamaged[] = "Chrome’s Bookmarks file is damaged.";

BookmarksResult ParseChromeBookmarks(std::string_view text,
                                     std::string_view profile_dir);

}  // namespace clu

#endif  // CLU_BROWSER_IMPORTER_CLU_CHROME_BOOKMARKS_H_
