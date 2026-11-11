// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_MODEL_CLU_ROW_STAMP_H_
#define CLU_BROWSER_MODEL_CLU_ROW_STAMP_H_

#include <optional>
#include <string>
#include <string_view>

namespace content {
class NavigationEntry;
}  // namespace content

namespace sessions {
class ContentSerializedNavigationDriver;
}  // namespace sessions

// The row id a page's history carries through Chromium's own restore paths (docs/chromium/fixes.md).
// Each entry of a row's page holds the row id; a registered sessions::ExtendedInfoHandler writes it into the
// SerializedNavigationEntry extended info that the session file (crash restore, "Continue where you left off",
// chrome://restart) and the tab restore file (History > Recently Closed) already keep, and puts it back on the
// restored entries. CluTabId then gives the restored page its old row id, so it goes back to its row.
namespace clu {

// The extended info key in both files. Never change it: files written by earlier builds use it.
inline constexpr char kRowStampKey[] = "clu.row";

// The row id `info` names, lowercased, or nothing for junk (a damaged or hand-edited file).
std::optional<std::string> ParseRowStamp(std::string_view info);

// Registers the handler on `driver` once; later calls do nothing. Call before the first session or tab restore.
void RegisterRowStampHandler(sessions::ContentSerializedNavigationDriver* driver);

// `row_id` must be a row id (ParseRowStamp accepts it).
void StampEntry(content::NavigationEntry* entry, const std::string& row_id);
// The row id on `entry`, if any.
std::optional<std::string> StampOf(content::NavigationEntry* entry);

}  // namespace clu

#endif  // CLU_BROWSER_MODEL_CLU_ROW_STAMP_H_
