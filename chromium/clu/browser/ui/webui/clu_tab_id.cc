// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_tab_id.h"

#include <optional>
#include <utility>

#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "clu/browser/model/clu_row_stamp.h"
#include "clu/browser/ui/webui/clu_tab_hooks.h"
#include "components/sessions/content/content_serialized_navigation_builder.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace clu {

namespace {

// The row id the page's restored history carries: the shown entry's, else any entry's.
std::optional<std::string> RowIdInHistory(content::WebContents* contents) {
  content::NavigationController& controller = contents->GetController();
  const int current = controller.GetCurrentEntryIndex();
  if (current >= 0) {
    if (std::optional<std::string> id =
            StampOf(controller.GetEntryAtIndex(current))) {
      return id;
    }
  }
  for (int i = 0; i < controller.GetEntryCount(); ++i) {
    if (std::optional<std::string> id = StampOf(controller.GetEntryAtIndex(i))) {
      return id;
    }
  }
  return std::nullopt;
}

}  // namespace

CluTabId::CluTabId(content::WebContents* contents,
                   std::string id,
                   bool restored)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<CluTabId>(*contents),
      id_(std::move(id)),
      restored_(restored) {
  StampHistory();
}

CluTabId::~CluTabId() = default;

// static
const std::string& CluTabId::Get(content::WebContents* contents) {
  if (!FromWebContents(contents)) {
    if (std::optional<std::string> restored = RowIdInHistory(contents)) {
      CreateForWebContents(contents, *restored, /*restored=*/true);
    } else {
      CreateForWebContents(contents,
                           base::Uuid::GenerateRandomV4().AsLowercaseString(),
                           /*restored=*/false);
    }
  }
  return FromWebContents(contents)->id_;
}

// static
void CluTabId::Set(content::WebContents* contents, const std::string& id) {
  contents->RemoveUserData(UserDataKey());
  CreateForWebContents(contents, id, /*restored=*/false);
}

// static
bool CluTabId::Has(content::WebContents* contents) {
  return FromWebContents(contents) != nullptr;
}

// static
bool CluTabId::WasRestored(content::WebContents* contents) {
  const CluTabId* tab_id = FromWebContents(contents);
  return tab_id && tab_id->restored_;
}

// static
bool CluTabId::TakeRestored(content::WebContents* contents) {
  CluTabId* tab_id = FromWebContents(contents);
  if (!tab_id || !tab_id->restored_) {
    return false;
  }
  tab_id->restored_ = false;
  return true;
}

void CluTabId::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  StampHistory();
}

void CluTabId::StampHistory() {
  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }
  content::NavigationController& controller = contents->GetController();
  SessionService* session_service = SessionServiceFactory::GetForProfileIfExisting(
      Profile::FromBrowserContext(contents->GetBrowserContext()));
  const SessionID window_id =
      sessions::SessionTabHelper::IdForWindowContainingTab(contents);
  const SessionID tab_id = sessions::SessionTabHelper::IdForTab(contents);
  for (int i = 0; i < controller.GetEntryCount(); ++i) {
    content::NavigationEntry* entry = controller.GetEntryAtIndex(i);
    if (!entry || StampOf(entry) == id_) {
      continue;
    }
    StampEntry(entry, id_);
    if (session_service && window_id.is_valid() && tab_id.is_valid() &&
        !entry->IsInitialEntry()) {
      session_service->UpdateTabNavigation(
          window_id, tab_id,
          sessions::ContentSerializedNavigationBuilder::FromNavigationEntry(
              i, entry));
    }
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CluTabId);

CluPlaceholder::CluPlaceholder(content::WebContents* contents)
    : content::WebContentsUserData<CluPlaceholder>(*contents) {}

CluPlaceholder::~CluPlaceholder() = default;

// static
void CluPlaceholder::Mark(content::WebContents* contents) {
  CreateForWebContents(contents);
}

// static
bool CluPlaceholder::Is(content::WebContents* contents) {
  return contents && FromWebContents(contents) != nullptr;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CluPlaceholder);

CluPageName::CluPageName(content::WebContents* contents, std::string name)
    : content::WebContentsUserData<CluPageName>(*contents),
      name_(std::move(name)) {}

CluPageName::~CluPageName() = default;

// static
void CluPageName::Set(content::WebContents* contents, const std::string& name) {
  contents->RemoveUserData(UserDataKey());
  if (!name.empty()) {
    CreateForWebContents(contents, name);
  }
}

// static
std::optional<std::string> CluPageName::Get(content::WebContents* contents) {
  const CluPageName* page = contents ? FromWebContents(contents) : nullptr;
  return page ? std::optional<std::string>(page->name_) : std::nullopt;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CluPageName);

bool IsPlaceholderTab(content::WebContents* contents) {
  return CluPlaceholder::Is(contents);
}

bool IsNewTabPageContents(content::WebContents* contents) {
  const GURL& url = contents->GetVisibleURL();
  return url.SchemeIs("chrome") &&
         (url.host() == "newtab" || url.host() == "new-tab-page" ||
          url.host() == "new-tab-page-third-party") &&
         contents->GetController().GetEntryCount() <= 1;
}

}  // namespace clu
