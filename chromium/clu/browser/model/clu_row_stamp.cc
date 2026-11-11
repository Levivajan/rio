// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_row_stamp.h"

#include <memory>
#include <utility>

#include "base/strings/string_util.h"
#include "base/supports_user_data.h"
#include "clu/browser/ui/webui/bridge/validation.h"
#include "components/sessions/content/content_serialized_navigation_driver.h"
#include "components/sessions/content/extended_info_handler.h"
#include "content/public/browser/navigation_entry.h"

namespace clu {
namespace {

const char kStampDataKey = 0;

// Not clonable on purpose: an entry Chromium copies into another page (a duplicate, a discard's new WebContents)
// starts without a row, and the page that takes it decides its id.
class RowStamp : public base::SupportsUserData::Data {
 public:
  explicit RowStamp(std::string row_id) : row_id_(std::move(row_id)) {}

  const std::string& row_id() const { return row_id_; }

 private:
  const std::string row_id_;
};

class RowStampHandler : public sessions::ExtendedInfoHandler {
 public:
  // sessions::ExtendedInfoHandler:
  std::string GetExtendedInfo(content::NavigationEntry* entry) const override {
    // An empty value writes nothing (ContentSerializedNavigationBuilder).
    return StampOf(entry).value_or(std::string());
  }

  void RestoreExtendedInfo(const std::string& info,
                           content::NavigationEntry* entry) override {
    if (std::optional<std::string> row_id = ParseRowStamp(info)) {
      StampEntry(entry, *row_id);
    }
  }
};

}  // namespace

std::optional<std::string> ParseRowStamp(std::string_view info) {
  if (!IsUuidV4(info)) {
    return std::nullopt;
  }
  return base::ToLowerASCII(info);
}

void RegisterRowStampHandler(
    sessions::ContentSerializedNavigationDriver* driver) {
  static bool registered = false;
  if (registered || !driver) {
    return;
  }
  registered = true;
  driver->RegisterExtendedInfoHandler(kRowStampKey,
                                      std::make_unique<RowStampHandler>());
}

void StampEntry(content::NavigationEntry* entry, const std::string& row_id) {
  if (!entry) {
    return;
  }
  entry->SetUserData(&kStampDataKey, std::make_unique<RowStamp>(row_id));
}

std::optional<std::string> StampOf(content::NavigationEntry* entry) {
  if (!entry) {
    return std::nullopt;
  }
  const auto* stamp =
      static_cast<const RowStamp*>(entry->GetUserData(&kStampDataKey));
  if (!stamp) {
    return std::nullopt;
  }
  return stamp->row_id();
}

}  // namespace clu
