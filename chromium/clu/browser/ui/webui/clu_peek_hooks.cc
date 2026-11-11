// Copyright 2026 The Rio Authors. All rights reserved.

// The Peek and Mini window hooks of clu/browser/multitask/clu_multitask_hooks.h
// (docs/chromium/m6.md section 3): chrome::Navigate() asks InterceptNavigate()
// first, and CreateAndAddChromeThrottlesForNavigation() adds CluPeekThrottle to
// every primary main-frame navigation.

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/multitask/clu_multitask_hooks.h"
#include "clu/browser/multitask/clu_peek_rules.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/common/clu_urls.h"
#include "clu/browser/ui/views/frame/clu_shell.h"
#include "clu/browser/ui/webui/clu_mini_windows.h"
#include "clu/browser/ui/webui/clu_peek.h"
#include "clu/browser/ui/webui/clu_settings_window.h"
#include "clu/browser/ui/webui/clu_tab_id.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace clu {

namespace {

// Settings, Links (docs/chromium/parity-settings.md): whether a Shift-click and a link to another site from a pinned
// tab or favorite open in Peek.
void ApplyLinkSettings(PeekInput& input) {
  if (CluAppSettings* settings = CluAppSettings::Get()) {
    input.peek_on_shift_click = settings->GetBool(kLinksPeekOnShiftClickKey);
    input.peek_other_sites = settings->GetBool(kLinksPeekOtherSitesKey);
  }
}

// The Clu window whose tab strip holds `contents`, or null (a Peek page, a
// WebUI, a tab of a window without Clu).
CluWindowState* WindowOfTab(content::WebContents* contents) {
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  BrowserWindowInterface* browser =
      tab ? tab->GetBrowserWindowInterface() : nullptr;
  return browser ? CluWindowState::Get(browser->GetUnownedUserDataHost())
                 : nullptr;
}

// The row a page belongs to, when the window keeps rows.
const TabNode* RowOf(CluWindowState* window, content::WebContents* contents) {
  CluModelService* model = window ? window->model() : nullptr;
  if (!model || !CluTabId::Has(contents)) {
    return nullptr;
  }
  const TabNode* row = model->tree().Get(CluTabId::Get(contents));
  return row && row->is_tab() ? row : nullptr;
}

PeekSource SourceOf(const TabNode& row) {
  switch (row.section) {
    case Section::kFavorite:
      return PeekSource::kFavorite;
    case Section::kPinned:
      return PeekSource::kPinned;
    case Section::kToday:
      return PeekSource::kNormal;
  }
  return PeekSource::kNormal;
}

PeekDisposition DispositionOf(WindowOpenDisposition disposition) {
  switch (disposition) {
    case WindowOpenDisposition::kCurrentTab:
      return PeekDisposition::kCurrentTab;
    case WindowOpenDisposition::kNewForegroundTab:
      return PeekDisposition::kForegroundTab;
    case WindowOpenDisposition::kNewBackgroundTab:
      return PeekDisposition::kBackgroundTab;
    case WindowOpenDisposition::kNewWindow:
      return PeekDisposition::kNewWindow;
    case WindowOpenDisposition::kNewPopup:
      return PeekDisposition::kNewPopup;
    default:
      return PeekDisposition::kOther;
  }
}

// A plain cross-host link click in a pinned tab or a favorite: the navigation
// is cancelled and the URL opens in the window's Peek, so the source tab's URL
// and history stay as they were (05 "Trigger: plain link click"). Form posts,
// GET forms, script redirects and navigations without a gesture go on.
class CluPeekThrottle : public content::NavigationThrottle {
 public:
  explicit CluPeekThrottle(content::NavigationThrottleRegistry& registry)
      : content::NavigationThrottle(registry) {}
  CluPeekThrottle(const CluPeekThrottle&) = delete;
  CluPeekThrottle& operator=(const CluPeekThrottle&) = delete;
  ~CluPeekThrottle() override = default;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override {
    content::NavigationHandle* handle = navigation_handle();
    content::WebContents* contents = handle->GetWebContents();
    CluWindowState* window = WindowOfTab(contents);
    const TabNode* row = RowOf(window, contents);
    if (!row || window->shell().is_mini() || handle->IsSameDocument() ||
        handle->IsHistory()) {
      return PROCEED;
    }
    PeekInput input;
    input.source = SourceOf(*row);
    input.source_url = contents->GetLastCommittedURL();
    input.target_url = handle->GetURL();
    input.via = PeekVia::kNavigate;
    input.disposition = PeekDisposition::kCurrentTab;
    input.link_click = handle->IsRendererInitiated() &&
                       handle->WasInitiatedByLinkClick() &&
                       handle->HasUserGesture();
    input.post = handle->IsPost();
    input.form_submission = handle->IsFormSubmission();
    ApplyLinkSettings(input);
    if (ShouldPeek(input) != PeekDecision::kPeek) {
      return PROCEED;
    }
    // The Peek opens once the throttle has answered: the navigation must not
    // change windows under itself.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<CluWindowState> window, std::string row_id,
               GURL url, content::Referrer referrer) {
              if (window) {
                window->multitask().peek().Open(row_id, url, referrer);
              }
            },
            window->GetWeakPtr(), row->id, handle->GetURL(),
            content::Referrer(handle->GetReferrer())));
    return CANCEL_AND_IGNORE;
  }
  const char* GetNameForLogging() override { return "CluPeekThrottle"; }
};

}  // namespace

void MaybeAddPeekThrottle(content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame() || !WindowOfTab(handle.GetWebContents())) {
    return;
  }
  registry.AddThrottle(std::make_unique<CluPeekThrottle>(registry));
}

bool InterceptNavigate(NavigateParams* params) {
  content::WebContents* source =
      params ? params->source_contents.get() : nullptr;
  CluWindowState* window = WindowOfTab(source);
  if (!window) {
    return false;
  }
  // Chromium's settings link to Rio's AI features ("AI innovations",
  // chrome://settings/ai-features): they open in the app settings
  // window's AI tab, never as a tab. Rio's own settings page never loads in a
  // tab either.
  const GURL& from = source->GetLastCommittedURL();
  const bool from_settings =
      from.SchemeIs(content::kChromeUIScheme) && from.host() == "settings";
  if (params->url.SchemeIs(content::kChromeUIScheme) &&
      (params->url.host() == kCluSettingsHost ||
       (from_settings && params->url.host() == "settings" &&
        params->url.path() == "/ai-features"))) {
    if (from_settings) {
      CluSettingsWindow::Show(window->browser(), "ai");
    }
    return true;
  }
  const bool sized = params->window_features.has_width ||
                     params->window_features.has_height ||
                     params->window_features.is_popup;

  // A Mini window holds one page: a link it opens in a new tab or window opens
  // in another Mini window (05 "Limits and lifecycle"). Pages the page made
  // itself (window.open with an opener) and sized popups stay Chromium's.
  if (window->shell().is_mini()) {
    const WindowOpenDisposition d = params->disposition;
    if (params->contents_to_insert || sized ||
        (d != WindowOpenDisposition::kNewForegroundTab &&
         d != WindowOpenDisposition::kNewBackgroundTab &&
         d != WindowOpenDisposition::kNewWindow)) {
      return false;
    }
    // Handled whenever a page opened, in a Mini window or (at the cap) as a
    // Today tab, so Chromium never opens it a second time.
    params->navigated_or_inserted_contents = mini_windows::Open(
        window->profile(), params->url, /*command_bar=*/false);
    return params->navigated_or_inserted_contents != nullptr;
  }

  const TabNode* row = RowOf(window, source);
  if (!row) {
    return false;
  }
  PeekInput input;
  input.source = SourceOf(*row);
  input.source_url = source->GetLastCommittedURL();
  input.target_url = params->url;
  input.via =
      params->contents_to_insert ? PeekVia::kNewContents : PeekVia::kOpenUrl;
  input.disposition = DispositionOf(params->disposition);
  input.sized = sized;
  input.post = params->post_data != nullptr;
  input.from_browser =
      !params->is_renderer_initiated || params->started_from_context_menu;
  ApplyLinkSettings(input);
  // chrome::AddWebContents() forces user_gesture; the page's own gesture is the
  // original one.
  const bool gesture = params->contents_to_insert
                           ? params->original_user_gesture
                           : params->user_gesture;
  if (!gesture || ShouldPeek(input) != PeekDecision::kPeek) {
    return false;
  }
  CluPeek& peek = window->multitask().peek();
  if (params->contents_to_insert) {
    params->navigated_or_inserted_contents =
        peek.Adopt(row->id, std::move(params->contents_to_insert));
  } else {
    peek.Open(row->id, params->url, params->referrer);
    params->navigated_or_inserted_contents = peek.page();
  }
  return params->navigated_or_inserted_contents != nullptr;
}

}  // namespace clu
