// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/importer/clu_chrome_bookmarks.h"

#include <algorithm>
#include <utility>

#include "base/json/json_reader.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "clu/browser/importer/clu_import_rules.h"

namespace clu {

namespace {

struct Root {
  const char* key;
  const char* title;
};
constexpr Root kRoots[] = {
    {"bookmark_bar", "Bookmarks bar"},
    {"other", "Other bookmarks"},
    {"synced", "Mobile bookmarks"},
};

bool IsWordOrDash(std::string_view s, size_t max) {
  if (s.empty() || s.size() > max) {
    return false;
  }
  for (char c : s) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '_' && c != '-') {
      return false;
    }
  }
  return true;
}

bool IsDigits(std::string_view s, size_t max) {
  return !s.empty() && s.size() <= max &&
         std::all_of(s.begin(), s.end(),
                     [](char c) { return base::IsAsciiDigit(c); });
}

std::string NodeKey(const base::DictValue& node, const std::string& path) {
  if (const std::string* guid = node.FindString("guid");
      guid && IsWordOrDash(*guid, 64)) {
    return base::StrCat({"guid:", *guid});
  }
  if (const std::string* id = node.FindString("id"); id && IsDigits(*id, 20)) {
    return base::StrCat({"id:", *id});
  }
  return base::StrCat({"path:", path});
}

}  // namespace

ImportNode::ImportNode() = default;
ImportNode::ImportNode(const ImportNode&) = default;
ImportNode& ImportNode::operator=(const ImportNode&) = default;
ImportNode::ImportNode(ImportNode&&) = default;
ImportNode& ImportNode::operator=(ImportNode&&) = default;
ImportNode::~ImportNode() = default;

BookmarksResult::BookmarksResult() = default;
BookmarksResult::BookmarksResult(BookmarksResult&&) = default;
BookmarksResult& BookmarksResult::operator=(BookmarksResult&&) = default;
BookmarksResult::~BookmarksResult() = default;

BookmarksResult ParseChromeBookmarks(std::string_view text,
                                     std::string_view profile_dir) {
  BookmarksResult result;
  std::optional<base::Value> data = base::JSONReader::Read(
      text, base::JSON_PARSE_RFC, kMaxImportJsonDepth);
  const base::DictValue* roots =
      data && data->is_dict() ? data->GetDict().FindDict("roots") : nullptr;
  if (!roots) {
    result.error = kBookmarksDamaged;
    return result;
  }
  auto scope = [&](std::string_view key) {
    return base::StrCat({profile_dir, "/", key});
  };

  std::vector<ImportNode>& out = result.nodes;
  ImportNode root;
  root.key = scope("root");
  root.folder = true;
  root.title = kChromeImportFolderTitle;
  out.push_back(root);
  size_t items = 0;

  // An iterative walk: a hostile file may nest as deep as the reader allows. Each step visits one Chrome node, in
  // order; `depth` is the level a folder made here would have (the top folder is 1, "Bookmarks bar" is 2).
  struct Step {
    raw_ptr<const base::Value> node;
    std::string parent_key;
    int depth;
    std::string path;
  };
  for (const Root& r : kRoots) {
    const base::DictValue* raw = roots->FindDict(r.key);
    const base::ListValue* children = raw ? raw->FindList("children") : nullptr;
    if (!children || children->empty()) {
      continue;
    }
    const size_t top_index = out.size();
    ImportNode top;
    top.key = scope(base::StrCat({"root/", r.key}));
    top.parent_key = root.key;
    top.folder = true;
    top.title = r.title;
    out.push_back(top);

    std::vector<Step> stack;
    for (size_t i = children->size(); i-- > 0;) {
      stack.push_back({&(*children)[i], top.key, 3,
                       base::StrCat({r.key, "/", base::NumberToString(i)})});
    }
    while (!stack.empty()) {
      Step step = std::move(stack.back());
      stack.pop_back();
      const base::DictValue* node = step.node->GetIfDict();
      if (!node) {
        ++result.skipped;
        continue;
      }
      const std::string* type = node->FindString("type");
      const base::ListValue* kids = node->FindList("children");
      if (type && *type == "folder" && kids) {
        std::string into = step.parent_key;
        const bool make = step.depth <= kMaxImportFolderDepth;
        if (make) {
          if (items >= kMaxImportItems) {
            ++result.skipped;
            continue;
          }
          ++items;
          ++result.folders;
          ImportNode folder;
          folder.key = scope(NodeKey(*node, step.path));
          folder.parent_key = step.parent_key;
          folder.folder = true;
          folder.title = ImportTitle(node->Find("name"));
          if (folder.title.empty()) {
            folder.title = kUntitledFolderTitle;
          }
          into = folder.key;
          out.push_back(std::move(folder));
        }
        const int next = make ? step.depth + 1 : step.depth;
        for (size_t i = kids->size(); i-- > 0;) {
          stack.push_back({&(*kids)[i], into, next,
                           base::StrCat({step.path, "/", base::NumberToString(i)})});
        }
        continue;
      }
      const std::optional<std::string> url =
          type && *type == "url" ? ImportUrl(node->Find("url")) : std::nullopt;
      if (!url || items >= kMaxImportItems) {
        ++result.skipped;
        continue;
      }
      ++items;
      ++result.bookmarks;
      ImportNode tab;
      tab.key = scope(NodeKey(*node, step.path));
      tab.parent_key = step.parent_key;
      tab.url = *url;
      tab.title = ImportTitle(node->Find("name"));
      if (tab.title.empty()) {
        tab.title = ImportHost(*url);
      }
      out.push_back(std::move(tab));
    }
    // A root folder with nothing kept in it is left out.
    if (out.size() == top_index + 1) {
      out.pop_back();
    }
  }
  if (out.size() == 1) {
    out.clear();
  }
  return result;
}

}  // namespace clu
