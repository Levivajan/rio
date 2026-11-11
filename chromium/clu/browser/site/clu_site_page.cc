// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/site/clu_site_page.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/site/clu_blocker.h"
#include "clu/browser/site/clu_site_extension.h"
#include "clu/browser/site/clu_site_key.h"
#include "clu/browser/site/clu_site_profile.h"
#include "clu/browser/site/clu_site_service.h"
#include "clu/grit/clu_overlay_resources.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_api_frame_id_map.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/mojom/code_injection.mojom.h"
#include "extensions/common/mojom/css_origin.mojom-shared.h"
#include "extensions/common/mojom/execution_world.mojom-shared.h"
#include "extensions/common/mojom/host_id.mojom.h"
#include "extensions/common/mojom/match_origin_as_fallback.mojom-shared.h"
#include "extensions/common/mojom/run_location.mojom-shared.h"
#include "third_party/blink/public/mojom/script/script_evaluation_params.mojom-shared.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"

namespace clu {

namespace {

namespace emojom = extensions::mojom;

// The bar's update and the picker's teardown (src/renderer/page/zap/picker.ts updateScript and STOP_SCRIPT).
constexpr char kZapUpdatePrefix[] =
    "globalThis.__cluZap && globalThis.__cluZap.update(";
constexpr char kZapUpdateSuffix[] = ");\nvoid 0";
constexpr char kZapStopScript[] =
    "typeof globalThis.__cluZapStop === 'function' && "
    "globalThis.__cluZapStop();\nvoid 0";
constexpr size_t kMaxPickLabel = 400;
constexpr int kMaxPickMatches = 1000000;

std::set<CluSitePage*>& Pages() {
  static base::NoDestructor<std::set<CluSitePage*>> pages;
  return *pages;
}

base::RepeatingCallbackList<void(content::WebContents*)>& ChangedCallbacks() {
  static base::NoDestructor<
      base::RepeatingCallbackList<void(content::WebContents*)>>
      callbacks;
  return *callbacks;
}

std::optional<std::string>& ZapScriptOverride() {
  static base::NoDestructor<std::optional<std::string>> script;
  return *script;
}

const std::string& ZapScript() {
  if (const std::optional<std::string>& test = ZapScriptOverride()) {
    return *test;
  }
  static base::NoDestructor<std::string> script(
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_CLU_OVERLAY_PAGE_ZAP_JS));
  return *script;
}

emojom::HostID CluHost() {
  return emojom::HostID(emojom::HostID::HostType::kExtensions, kCluExtensionId);
}

Profile* OriginalOf(content::BrowserContext* context) {
  return Profile::FromBrowserContext(context)->GetOriginalProfile();
}

// The bar the picker shows: the site's hidden count and the last result line.
std::string ZapBarJson(const CluSiteService* service,
                              const std::optional<std::string>& site,
                              const std::optional<std::string>& message) {
  const SiteTweak* tweak = site && service ? service->Tweak(*site) : nullptr;
  base::DictValue bar;
  bar.Set("count", tweak ? static_cast<int>(tweak->zaps.size()) : 0);
  bar.Set("message", message ? base::Value(*message) : base::Value());
  return base::WriteJson(bar).value_or("{\"count\":0,\"message\":null}");
}

}  // namespace

// static
CluSitePage* CluSitePage::GetOrCreate(content::WebContents* contents) {
  if (!contents || !GetCluSiteService(contents->GetBrowserContext())) {
    return nullptr;
  }
  CreateForWebContents(contents);
  return FromWebContents(contents);
}

// static
CluSitePage* CluSitePage::Get(content::WebContents* contents) {
  return contents ? FromWebContents(contents) : nullptr;
}

// static
std::vector<CluSitePage*> CluSitePage::PagesOnSite(
    content::BrowserContext* context,
    std::string_view site) {
  std::vector<CluSitePage*> out;
  Profile* original = OriginalOf(context);
  for (CluSitePage* page : Pages()) {
    if (page->site_ && *page->site_ == site &&
        OriginalOf(page->web_contents()->GetBrowserContext()) == original) {
      out.push_back(page);
    }
  }
  return out;
}

// static
void CluSitePage::OnTweakChanged(content::BrowserContext* context,
                                 std::string_view site) {
  for (CluSitePage* page : PagesOnSite(context, site)) {
    page->ApplyTweakCss();
    page->UpdateZapBar();
    page->NotifyChanged();
  }
}

// static
void CluSitePage::OnTweaksSwitched(content::BrowserContext* context) {
  Profile* original = OriginalOf(context);
  CluSiteService* service = GetCluSiteService(context);
  std::vector<CluSitePage*> pages;
  for (CluSitePage* page : Pages()) {
    if (page->site_ &&
        OriginalOf(page->web_contents()->GetBrowserContext()) == original) {
      pages.push_back(page);
    }
  }
  for (CluSitePage* page : pages) {
    const SiteTweak* tweak = service ? service->Tweak(*page->site_) : nullptr;
    if (tweak && tweak->enabled &&
        !base::TrimWhitespaceASCII(tweak->js, base::TRIM_ALL).empty()) {
      page->web_contents()->GetController().Reload(
          content::ReloadType::NORMAL, /*check_for_repost=*/true);
      continue;
    }
    page->ApplyTweakCss();
    page->UpdateZapBar();
    page->NotifyChanged();
  }
}

// static
bool CluSitePage::TweaksEnabled() {
  CluAppSettings* settings = CluAppSettings::Get();
  return !settings || settings->GetBool(kSiteTweaksEnabledKey);
}

// static
void CluSitePage::ReloadSite(content::BrowserContext* context,
                             std::string_view site) {
  for (CluSitePage* page : PagesOnSite(context, site)) {
    page->web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                                 /*check_for_repost=*/true);
  }
}

// static
base::CallbackListSubscription CluSitePage::AddChangedCallback(
    base::RepeatingCallback<void(content::WebContents*)> callback) {
  return ChangedCallbacks().Add(std::move(callback));
}

// static
void CluSitePage::SetZapScriptForTesting(std::string script) {
  ZapScriptOverride() = std::move(script);
}

CluSitePage::CluSitePage(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<CluSitePage>(*contents),
      executor_(contents) {
  Pages().insert(this);
  site_ = SiteKey(contents->GetLastCommittedURL());
}

CluSitePage::~CluSitePage() {
  Pages().erase(this);
}

void CluSitePage::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame() || !navigation->HasCommitted() ||
      navigation->IsSameDocument()) {
    return;
  }
  // The old document took its sheets, its preview and its picker with it. One that went into the back/forward cache
  // keeps its sheets there.
  const content::GlobalRenderFrameHostId previous =
      navigation->GetPreviousRenderFrameHostId();
  const content::GlobalRenderFrameHostId current =
      navigation->GetRenderFrameHost()->GetGlobalId();
  if (previous != current && (cosmetic_sheet_ || tweak_sheet_)) {
    cached_sheets_[previous] = {std::move(cosmetic_sheet_),
                                std::move(tweak_sheet_)};
  }
  cosmetic_sheet_.reset();
  tweak_sheet_.reset();
  const bool restored = navigation->IsServedFromBackForwardCache();
  if (auto it = cached_sheets_.find(current); it != cached_sheets_.end()) {
    if (restored) {
      cosmetic_sheet_ = std::move(it->second.cosmetic);
      tweak_sheet_ = std::move(it->second.tweak);
    }
    cached_sheets_.erase(it);
  }
  draft_css_.reset();
  draft_controls_.reset();
  if (zap_active_) {
    EndZap(/*in_page=*/false);
  }
  const std::optional<std::string> old_site = site_;
  site_ = navigation->IsErrorPage() ? std::nullopt
                                    : SiteKey(navigation->GetURL());
  if (site_ != old_site) {
    NotifyChanged();
  }
  if (!site_) {
    return;
  }
  content::BrowserContext* context = web_contents()->GetBrowserContext();
  if (CluBlocker* blocker = GetCluBlocker(context)) {
    std::string css = blocker->CosmeticCss(navigation->GetURL());
    if (!cosmetic_sheet_ || cosmetic_sheet_->code != css) {
      RemoveSheet(cosmetic_sheet_);
      if (!css.empty()) {
        InsertSheet(std::move(css), cosmetic_sheet_, /*author=*/false);
      }
    }
  } else {
    RemoveSheet(cosmetic_sheet_);
  }
  ApplyTweakCss();
  // A restored document ran its tweak JS when it first loaded.
  if (!restored) {
    RunTweakJs();
  }
}

void CluSitePage::OnVisibilityChanged(content::Visibility visibility) {
  // Switching tabs ends zap mode in the tab left behind (08-extensions "Exit").
  if (visibility == content::Visibility::HIDDEN && zap_active_) {
    StopZap();
  }
}

void CluSitePage::RenderFrameDeleted(content::RenderFrameHost* frame) {
  // A cached document left the cache (evicted, or its tab closed).
  cached_sheets_.erase(frame->GetGlobalId());
}

void CluSitePage::WebContentsDestroyed() {
  Pages().erase(this);
}

void CluSitePage::PreviewCss(std::string css) {
  draft_css_ = std::move(css);
  ApplyTweakCss();
}

void CluSitePage::PreviewControls(const TweakControls& controls) {
  draft_controls_ = controls;
  ApplyTweakCss();
}

void CluSitePage::EndPreview() {
  if (!draft_css_ && !draft_controls_) {
    return;
  }
  ClearDrafts();
}

void CluSitePage::ClearDrafts() {
  draft_css_.reset();
  draft_controls_.reset();
  ApplyTweakCss();
}

void CluSitePage::ApplyTweakCss() {
  CluSiteService* service = GetCluSiteService(web_contents()->GetBrowserContext());
  std::string css;
  // With Site tweaks off everywhere, a page shows none, drafts included, as with the site's own tweak off.
  if (site_ && service && TweaksEnabled()) {
    css = TweakCss(service->Tweak(*site_), draft_css_, draft_controls_);
  }
  if (tweak_sheet_ && tweak_sheet_->code == css) {
    return;
  }
  RemoveSheet(tweak_sheet_);
  if (!css.empty()) {
    InsertSheet(std::move(css), tweak_sheet_, /*author=*/true);
  }
}

void CluSitePage::InsertSheet(std::string css,
                              std::optional<Sheet>& slot,
                              bool author) {
  const emojom::HostID host = CluHost();
  Sheet sheet{.code = css,
              .key = extensions::ScriptExecutor::GenerateInjectionKey(
                  host, GURL(), css)};
  std::vector<emojom::CSSSourcePtr> sources;
  sources.push_back(emojom::CSSSource::New(std::move(css), sheet.key));
  executor_.ExecuteScript(
      host,
      emojom::CodeInjection::NewCss(emojom::CSSInjection::New(
          std::move(sources),
          author ? emojom::CSSOrigin::kAuthor : emojom::CSSOrigin::kUser,
          emojom::CSSInjection::Operation::kAdd)),
      extensions::ScriptExecutor::SPECIFIED_FRAMES,
      {extensions::ExtensionApiFrameIdMap::kTopFrameId},
      emojom::MatchOriginAsFallbackBehavior::kNever,
      emojom::RunLocation::kDocumentStart,
      extensions::ScriptExecutor::DEFAULT_PROCESS, GURL(), base::DoNothing());
  sheet.author = author;
  slot = std::move(sheet);
}

void CluSitePage::RemoveSheet(std::optional<Sheet>& slot) {
  if (!slot) {
    return;
  }
  std::vector<emojom::CSSSourcePtr> sources;
  sources.push_back(emojom::CSSSource::New(slot->code, slot->key));
  const bool author = slot->author;
  slot.reset();
  executor_.ExecuteScript(
      CluHost(),
      emojom::CodeInjection::NewCss(emojom::CSSInjection::New(
          std::move(sources),
          author ? emojom::CSSOrigin::kAuthor : emojom::CSSOrigin::kUser,
          emojom::CSSInjection::Operation::kRemove)),
      extensions::ScriptExecutor::SPECIFIED_FRAMES,
      {extensions::ExtensionApiFrameIdMap::kTopFrameId},
      emojom::MatchOriginAsFallbackBehavior::kNever,
      emojom::RunLocation::kDocumentStart,
      extensions::ScriptExecutor::DEFAULT_PROCESS, GURL(), base::DoNothing());
}

void CluSitePage::RunJs(
    std::string code,
    bool want_result,
    extensions::ScriptExecutor::ScriptFinishedCallback callback) {
  // ScriptExecutor drops the call, callback and all, when the extension is not loaded.
  if (!extensions::ExtensionRegistry::Get(web_contents()->GetBrowserContext())
           ->enabled_extensions()
           .Contains(kCluExtensionId)) {
    std::move(callback).Run({});
    return;
  }
  std::vector<emojom::JSSourcePtr> sources;
  sources.push_back(emojom::JSSource::New(std::move(code), GURL()));
  // The component extension's isolated world: the page's own scripts cannot see or call into it.
  executor_.ExecuteScript(
      CluHost(),
      emojom::CodeInjection::NewJs(emojom::JSInjection::New(
          std::move(sources), emojom::ExecutionWorld::kIsolated,
          /*world_id=*/std::nullopt,
          want_result ? blink::mojom::WantResultOption::kWantResult
                      : blink::mojom::WantResultOption::kNoResult,
          blink::mojom::UserActivationOption::kDoNotActivate,
          want_result ? blink::mojom::PromiseResultOption::kAwait
                      : blink::mojom::PromiseResultOption::kDoNotWait)),
      extensions::ScriptExecutor::SPECIFIED_FRAMES,
      {extensions::ExtensionApiFrameIdMap::kTopFrameId},
      emojom::MatchOriginAsFallbackBehavior::kNever,
      emojom::RunLocation::kDocumentIdle,
      extensions::ScriptExecutor::DEFAULT_PROCESS, GURL(),
      std::move(callback));
}

void CluSitePage::RunTweakJs() {
  CluSiteService* service = GetCluSiteService(web_contents()->GetBrowserContext());
  const SiteTweak* tweak = site_ && service ? service->Tweak(*site_) : nullptr;
  if (!tweak || !tweak->enabled || !TweaksEnabled() ||
      base::TrimWhitespaceASCII(tweak->js, base::TRIM_ALL).empty()) {
    return;
  }
  RunJs(WrapTweakJs(tweak->js), /*want_result=*/false, base::DoNothing());
}

// Zap mode -------------------------------------------------------------------------------------------------------------

bool CluSitePage::StartZap() {
  if (!site_) {
    return false;
  }
  if (zap_active_) {
    return true;
  }
  zap_active_ = true;
  ++zap_generation_;
  zap_last_result_.reset();
  NotifyChanged();
  PickNext();
  return true;
}

void CluSitePage::StopZap() {
  if (!zap_active_) {
    return;
  }
  EndZap(/*in_page=*/true);
}

void CluSitePage::EndZap(bool in_page) {
  zap_active_ = false;
  ++zap_generation_;
  zap_last_result_.reset();
  if (in_page) {
    RunJs(kZapStopScript, /*want_result=*/false, base::DoNothing());
  }
  NotifyChanged();
}

void CluSitePage::UndoZap() {
  CluSiteService* service = GetCluSiteService(web_contents()->GetBrowserContext());
  if (!site_ || !service) {
    return;
  }
  const std::optional<Zap> last = service->UndoZap(*site_);
  if (zap_active_) {
    SetZapResult(last ? base::StrCat({"Restored ",
                                      last->label.empty() ? "an element"
                                                          : last->label})
                      : std::string("Nothing to undo"));
  }
}

void CluSitePage::PickNext() {
  CluSiteService* service = GetCluSiteService(web_contents()->GetBrowserContext());
  const std::string code =
      base::StrCat({ZapScript(), "\n;globalThis.__cluZap.update(",
                    ZapBarJson(service, site_, zap_last_result_),
                    ");\nglobalThis.__cluZap.pick()"});
  RunJs(code, /*want_result=*/true,
        base::BindOnce(&CluSitePage::OnPick, weak_ptr_factory_.GetWeakPtr(),
                       zap_generation_));
}

void CluSitePage::OnPick(
    int generation,
    std::vector<extensions::ScriptExecutor::FrameResult> results) {
  if (!zap_active_ || generation != zap_generation_) {
    return;
  }
  // The document went away under the pending pick, or the extension is gone.
  if (results.empty() || !results[0].error.empty() ||
      !results[0].value.is_dict()) {
    // A picker that answers junk would otherwise stay in the page and swallow every click.
    EndZap(/*in_page=*/results.empty() ? false : true);
    return;
  }
  const base::DictValue& pick = results[0].value.GetDict();
  const std::string* kind = pick.FindString("kind");
  if (!kind) {
    StopZap();
    return;
  }
  if (*kind == "stopped") {
    EndZap(/*in_page=*/false);
    return;
  }
  if (*kind == "done") {
    StopZap();
    return;
  }
  if (*kind == "undo") {
    UndoZap();
    PickNext();
    return;
  }
  if (*kind == "zap") {
    HandleZap(pick);
    if (zap_active_) {
      PickNext();
    }
    return;
  }
  StopZap();
}

void CluSitePage::HandleZap(const base::DictValue& pick) {
  // The isolated world is out of the page's reach, but its answers are still checked like any payload.
  const std::string* selector = pick.FindString("selector");
  const std::string* label = pick.FindString("label");
  const std::optional<bool> related = pick.FindBool("related");
  const std::optional<double> matches = pick.FindDouble("matches");
  if (!selector || selector->size() > kMaxZapSelector || !label ||
      label->size() > kMaxPickLabel || !related || !matches ||
      !std::isfinite(*matches) || *matches < 0 || *matches > kMaxPickMatches) {
    StopZap();
    return;
  }
  CluSiteService* service = GetCluSiteService(web_contents()->GetBrowserContext());
  if (!site_ || !service) {
    EndZap(/*in_page=*/true);
    return;
  }
  // created_at 0: the service stamps it with its clock.
  const AppendZapResult result = service->AddZap(
      *site_, Zap{.selector = *selector, .label = *label, .related = *related});
  if (result == AppendZapResult::kFull) {
    SetZapResult(base::StrCat({"Rio hides at most ",
                               base::NumberToString(kMaxZaps),
                               " elements per site"}));
    return;
  }
  if (result == AppendZapResult::kUnsafe) {
    SetZapResult("Rio can’t hide this element");
    return;
  }
  const SiteTweak* tweak = service->Tweak(*site_);
  if (!TweaksEnabled()) {
    SetZapResult("Saved. Site tweaks are off in Settings");
  } else if (tweak && !tweak->enabled) {
    SetZapResult("Saved. Site tweaks are off for this site");
  } else if (*related) {
    const int n = static_cast<int>(*matches);
    SetZapResult(base::StrCat({"Hid ", base::NumberToString(n),
                               " related element", n == 1 ? "" : "s"}));
  } else {
    SetZapResult(std::nullopt);
  }
}

void CluSitePage::SetZapResult(std::optional<std::string> message) {
  zap_last_result_ = std::move(message);
  UpdateZapBar();
  NotifyChanged();
}

void CluSitePage::UpdateZapBar() {
  if (!zap_active_) {
    return;
  }
  CluSiteService* service = GetCluSiteService(web_contents()->GetBrowserContext());
  RunJs(base::StrCat({kZapUpdatePrefix,
                      ZapBarJson(service, site_, zap_last_result_),
                      kZapUpdateSuffix}),
        /*want_result=*/false, base::DoNothing());
}

void CluSitePage::NotifyChanged() {
  ChangedCallbacks().Notify(web_contents());
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(CluSitePage);

}  // namespace clu
