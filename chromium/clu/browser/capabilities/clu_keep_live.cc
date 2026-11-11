// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/capabilities/clu_keep_live.h"

#include <memory>
#include <tuple>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/supports_user_data.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "clu/browser/capabilities/clu_keep_live_rules.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/session_id.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace clu {

namespace {

// The mark on a tab whose auto-discardable flag Clu cleared.
const char kHeldKey[] = "clu-keep-live";

CluKeepLive* g_instance = nullptr;

}  // namespace

// static
void CluKeepLive::Start() {
  static base::NoDestructor<CluKeepLive> instance;
  std::ignore = *instance;
}

// static
CluKeepLive* CluKeepLive::Get() {
  return g_instance;
}

CluKeepLive::CluKeepLive() {
  g_instance = this;
  // Memory Saver is on unless the user turned it off (a user value wins over this default).
  if (PrefService* local_state = g_browser_process->local_state()) {
    local_state->SetDefaultPrefValue(
        performance_manager::user_tuning::prefs::kMemorySaverModeState,
        base::Value(static_cast<int>(performance_manager::user_tuning::prefs::
                                         MemorySaverModeState::kEnabled)));
  }
  GlobalBrowserCollection* browsers = GlobalBrowserCollection::GetInstance();
  browsers_observation_.Observe(browsers);
  browsers->ForEach([this](BrowserWindowInterface* browser) {
    Watch(browser);
    return true;
  });
}

CluKeepLive::~CluKeepLive() = default;

bool CluKeepLive::IsHeld(content::WebContents* contents) const {
  return contents && contents->GetUserData(kHeldKey);
}

void CluKeepLive::OnBrowserCreated(BrowserWindowInterface* browser) {
  Watch(browser);
}

void CluKeepLive::OnBrowserClosed(BrowserWindowInterface* browser) {
  if (!watched_.erase(browser->GetSessionID().id())) {
    return;
  }
  if (TabStripModel* tabs = browser->GetTabStripModel()) {
    tabs->RemoveObserver(this);
  }
}

void CluKeepLive::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kInserted ||
      change.type() == TabStripModelChange::kReplaced ||
      change.type() == TabStripModelChange::kMoved) {
    ScheduleApply();
  }
}

void CluKeepLive::OnSplitTabChanged(const SplitTabChange& change) {
  ScheduleApply();
}

void CluKeepLive::Watch(BrowserWindowInterface* browser) {
  TabStripModel* tabs = browser->GetTabStripModel();
  if (!tabs || !watched_.insert(browser->GetSessionID().id()).second) {
    return;
  }
  tabs->AddObserver(this);
  ScheduleApply();
}

void CluKeepLive::ScheduleApply() {
  // After every other observer of the change: a tab's lifecycle unit exists only once Chromium's own tab strip
  // observer has run.
  if (apply_pending_) {
    return;
  }
  apply_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&CluKeepLive::ApplyAll, base::Unretained(this)));
}

void CluKeepLive::ApplyAll() {
  apply_pending_ = false;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [this](BrowserWindowInterface* browser) {
        Apply(browser);
        return true;
      });
}

void CluKeepLive::Apply(BrowserWindowInterface* browser) {
  TabStripModel* tabs = browser->GetTabStripModel();
  if (!tabs) {
    return;
  }
  const bool normal = browser->GetType() == BrowserWindowInterface::TYPE_NORMAL;
  for (int i = 0; i < tabs->count(); ++i) {
    tabs::TabInterface* tab = tabs->GetTabAtIndex(i);
    if (!tab || !tab->GetContents()) {
      continue;
    }
    const KeepLiveFacts facts{.normal_window = normal, .split = tab->IsSplit()};
    Hold(tab->GetContents(), KeepLiveReason(facts).has_value());
  }
}

void CluKeepLive::Hold(content::WebContents* contents, bool hold) {
  auto* unit =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(contents);
  if (!unit) {
    return;
  }
  if (hold) {
    if (!IsHeld(contents) && unit->IsAutoDiscardable()) {
      unit->SetAutoDiscardable(false);
      contents->SetUserData(kHeldKey,
                            std::make_unique<base::SupportsUserData::Data>());
    }
    return;
  }
  // Only a flag Clu cleared goes back; an extension's stays.
  if (IsHeld(contents)) {
    contents->RemoveUserData(kHeldKey);
    unit->SetAutoDiscardable(true);
  }
}

}  // namespace clu
