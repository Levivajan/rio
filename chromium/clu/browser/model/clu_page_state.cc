// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_page_state.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/strings/utf_string_conversions.h"
#include "clu/browser/model/clu_nav_state.h"
#include "clu/browser/model/clu_tab_rules.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/restore_type.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace clu {

std::optional<std::string> CaptureNavState(content::WebContents* contents) {
  content::NavigationController& controller = contents->GetController();
  const int count = controller.GetEntryCount();
  const int current = controller.GetLastCommittedEntryIndex();
  if (count <= 0) {
    return std::nullopt;
  }
  NavState state;
  state.index = current < 0 ? count - 1 : current;
  for (int i = 0; i < count; ++i) {
    content::NavigationEntry* entry = controller.GetEntryAtIndex(i);
    if (!entry) {
      continue;
    }
    const sessions::SerializedNavigationEntry serialized =
        sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(
            i, entry);
    NavEntry nav;
    nav.url = serialized.virtual_url().spec();
    nav.title = base::UTF16ToUTF8(serialized.title());
    if (!serialized.encoded_page_state().empty()) {
      nav.page_state = base::Base64Encode(serialized.encoded_page_state());
    }
    state.entries.push_back(std::move(nav));
  }
  return CappedNavStateJson(std::move(state));
}

bool RestoreNavState(content::WebContents* contents, const std::string& json) {
  std::optional<NavState> state = ParseNavState(json);
  if (!state) {
    return false;
  }
  std::vector<sessions::SerializedNavigationEntry> navigations;
  for (size_t i = 0; i < state->entries.size(); ++i) {
    const NavEntry& nav = state->entries[i];
    const GURL url(nav.url);
    sessions::SerializedNavigationEntry entry;
    entry.set_index(static_cast<int>(i));
    entry.set_virtual_url(url);
    entry.set_original_request_url(url);
    entry.set_title(base::UTF8ToUTF16(nav.title));
    entry.set_transition_type(ui::PAGE_TRANSITION_LINK);
    std::string page_state;
    if (nav.page_state && base::Base64Decode(*nav.page_state, &page_state)) {
      entry.set_encoded_page_state(page_state);
    }
    navigations.push_back(std::move(entry));
  }
  std::vector<std::unique_ptr<content::NavigationEntry>> entries =
      sessions::ContentSerializedNavigationBuilder::ToNavigationEntries(
          navigations, contents->GetBrowserContext());
  if (entries.empty() || static_cast<size_t>(state->index) >= entries.size()) {
    return false;
  }
  contents->GetController().Restore(state->index,
                                    content::RestoreType::kRestored, &entries);
  return true;
}

std::string PageUrl(content::WebContents* contents) {
  const GURL& committed = contents->GetLastCommittedURL();
  if (committed.is_valid() && !committed.is_empty()) {
    return committed.spec();
  }
  if (content::NavigationEntry* pending =
          contents->GetController().GetPendingEntry()) {
    return pending->GetURL().spec();
  }
  return contents->GetVisibleURL().spec();
}

}  // namespace clu
