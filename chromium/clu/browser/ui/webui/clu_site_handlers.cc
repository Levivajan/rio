// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_site_handlers.h"

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/strings/string_util.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/webstore_install_with_prompt.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/extensions/extension_enable_flow.h"
#include "chrome/browser/ui/extensions/extension_enable_flow_delegate.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "clu/browser/importer/clu_import_service.h"
#include "clu/browser/site/clu_site_extension.h"
#include "clu/browser/site/clu_site_key.h"
#include "clu/browser/site/clu_site_page.h"
#include "clu/browser/site/clu_site_service.h"
#include "clu/browser/site/clu_tweak_rules.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_extension_actions.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/webui/clu_site_state.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_site.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "extensions/browser/disable_reason.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/ui_util.h"
#include "extensions/browser/webstore_install_result.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/gurl.h"

namespace clu {

namespace {

// An options page opens in a window of its own, never a tab (PLAN decision 57).
constexpr int kOptionsWidth = 680;
constexpr int kOptionsHeight = 560;

mojom::CallResultPtr WindowGone() {
  return ErrorResult(mojom::ErrorCode::kRejected, "the window is closing");
}

mojom::CallResultPtr TabNotFound() {
  return ErrorResult(mojom::ErrorCode::kNotFound, "no such tab here");
}

mojom::CallResultPtr ExtensionNotFound() {
  return ErrorResult(mojom::ErrorCode::kNotFound, "no such extension");
}

mojom::CallResultPtr NotReady() {
  return RejectedResult("not-ready");
}

mojom::CallResultPtr PrivateWindow() {
  return RejectedResult("private-window");
}

mojom::CallResultPtr NoSite() {
  return RejectedResult("no-site");
}

gfx::Rect RectOf(const mojom::Rect& rect) {
  return gfx::ToEnclosingRect(
      gfx::RectF(static_cast<float>(rect.x), static_cast<float>(rect.y),
                 static_cast<float>(std::max(0.0, rect.width)),
                 static_cast<float>(std::max(0.0, rect.height))));
}

// The controls of a payload, repaired like a stored row (the guard checked the ranges, not the font list).
TweakControls ControlsOf(const mojom::TweakControls& c) {
  return ClampControls(base::Value(
      base::DictValue()
          .Set("hue", c.hue)
          .Set("invert", c.invert)
          .Set("contrast", c.contrast)
          .Set("brightness", c.brightness)
          .Set("saturation", c.saturation)
          .Set("fontFamily", c.font_family ? base::Value(*c.font_family)
                                           : base::Value())
          .Set("fontScale", c.font_scale)));
}

bool HasJs(const std::string& js) {
  return !base::TrimWhitespaceASCII(js, base::TRIM_ALL).empty();
}

// The site of row `tab_id` with the window's site data, or the answer that says why there is none.
struct SiteTarget {
  raw_ptr<CluWindowSite> window_site = nullptr;
  raw_ptr<CluSiteService> service = nullptr;
  std::string site;
};

base::expected<SiteTarget, mojom::CallResultPtr> Target(
    CluWindowState& window,
    const std::string& tab_id,
    bool write) {
  CluWindowSite& w = window.site();
  if (!w.HasRow(tab_id)) {
    return base::unexpected(TabNotFound());
  }
  if (write && !w.can_write()) {
    return base::unexpected(PrivateWindow());
  }
  CluSiteService* service = w.service();
  if (!service || !service->loaded()) {
    return base::unexpected(NotReady());
  }
  std::optional<std::string> site = w.SiteOf(tab_id);
  if (!site) {
    return base::unexpected(NoSite());
  }
  return SiteTarget{.window_site = &w, .service = service, .site = *site};
}

// An extension chrome://extensions would list for the window's profile, enabled or not.
const extensions::Extension* InstalledExtension(Profile* profile,
                                                const std::string& id) {
  if (id == kCluExtensionId) {
    return nullptr;
  }
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile)->GetInstalledExtension(id);
  return extension &&
                 extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)
             ? extension
             : nullptr;
}

}  // namespace

// blocking.* ---------------------------------------------------------------------------------------------------------

CluBlockingHandler::CluBlockingHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluBlockingHandler::~CluBlockingHandler() = default;

void CluBlockingHandler::SetSiteEnabled(
    mojom::BlockingSetSiteEnabledInputPtr input,
    SetSiteEnabledCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  // The site's open pages reload once the exception is in place (CluBlocker).
  target->service->SetBlockingEnabled(target->site, input->enabled);
  std::move(callback).Run(NullResult());
}

// siteControls.* -----------------------------------------------------------------------------------------------------

CluSiteControlsHandler::CluSiteControlsHandler(
    base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluSiteControlsHandler::~CluSiteControlsHandler() = default;

void CluSiteControlsHandler::Open(mojom::SiteControlsOpenInputPtr input,
                                  OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->site().OpenSiteControls(RectOf(*input->anchor));
  std::move(callback).Run(NullResult());
}

void CluSiteControlsHandler::Share(mojom::SiteControlsShareInputPtr input,
                                   ShareCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->ShareUrl(input->anchor ? std::optional<gfx::Rect>(
                                        RectOf(*input->anchor))
                                  : std::nullopt);
  std::move(callback).Run(NullResult());
}

void CluSiteControlsHandler::Screenshot(ScreenshotCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // A page with nothing to capture says so in a toast.
  std::ignore = window_->TakeScreenshot();
  std::move(callback).Run(NullResult());
}

void CluSiteControlsHandler::SetDevMode(
    mojom::SiteControlsSetDevModeInputPtr input,
    SetDevModeCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  // Every window republishes from the pref, and the strip follows its front page.
  CluWindowSite::SetDevMode(target->site, input->on);
  std::move(callback).Run(NullResult());
}

void CluSiteControlsHandler::DevTool(mojom::SiteControlsDevToolInputPtr input,
                                     DevToolCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // Chromium's own commands on the window's active page: DevTools docks or undocks as the user last left it.
  int command = IDC_DEV_TOOLS;
  switch (input->tool) {
    case mojom::SiteControlsDevToolInputTool::kConsole:
      command = IDC_DEV_TOOLS_CONSOLE;
      break;
    case mojom::SiteControlsDevToolInputTool::kDevtools:
      command = IDC_DEV_TOOLS;
      break;
    case mojom::SiteControlsDevToolInputTool::kInspect:
      command = IDC_DEV_TOOLS_INSPECT;
      break;
    case mojom::SiteControlsDevToolInputTool::kHardReload:
      command = IDC_RELOAD_BYPASSING_CACHE;
      break;
  }
  BrowserWindowInterface* browser = window_->browser();
  if (!browser || !chrome::IsCommandEnabled(browser, command)) {
    std::move(callback).Run(RejectedResult("unavailable"));
    return;
  }
  chrome::ExecuteCommand(browser, command);
  std::move(callback).Run(NullResult());
}

void CluSiteControlsHandler::CaptureText(CaptureTextCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // A page with nothing to read says so in a toast.
  std::ignore = window_->CaptureText();
  std::move(callback).Run(NullResult());
}

// extensions.* -------------------------------------------------------------------------------------------------------

// Chromium's enable flow for one extension, answering the call when it finishes or is aborted (prompt declined, a
// disable reason the user can't clear, policy).
class CluExtensionsHandler::EnableRequest : public ExtensionEnableFlowDelegate {
 public:
  EnableRequest(Profile* profile,
                const std::string& id,
                base::OnceCallback<void(bool)> done)
      : flow_(profile, id, this), done_(std::move(done)) {}

  void Start(content::WebContents* parent) {
    if (parent) {
      flow_.StartForWebContents(parent);
    } else {
      flow_.Start();
    }
  }

  // ExtensionEnableFlowDelegate (each may delete this):
  void ExtensionEnableFlowFinished() override { std::move(done_).Run(true); }
  void ExtensionEnableFlowAborted(bool user_initiated) override {
    std::move(done_).Run(false);
  }

 private:
  ExtensionEnableFlow flow_;
  base::OnceCallback<void(bool)> done_;
};

CluExtensionsHandler::CluExtensionsHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluExtensionsHandler::~CluExtensionsHandler() = default;

void CluExtensionsHandler::List(mojom::ExtensionsListInputPtr input,
                                ListCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (input->profile_id != window_->site().profile_id()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "not this window's profile"));
    return;
  }
  std::move(callback).Run(ValueResult(
      base::Value(ExtensionsValue(BuildExtensionInfos(window_->profile())))));
}

void CluExtensionsHandler::Remove(mojom::ExtensionsRemoveInputPtr input,
                                  RemoveCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (input->profile_id != window_->site().profile_id() ||
      !InstalledExtension(window_->profile(), input->id)) {
    std::move(callback).Run(ExtensionNotFound());
    return;
  }
  std::u16string error;
  if (!extensions::ExtensionRegistrar::Get(window_->profile())
           ->UninstallExtension(input->id,
                                extensions::UNINSTALL_REASON_USER_INITIATED,
                                &error)) {
    std::move(callback).Run(RejectedResult("policy"));
    return;
  }
  std::move(callback).Run(NullResult());
}

void CluExtensionsHandler::OpenOptions(
    mojom::ExtensionsOpenOptionsInputPtr input,
    OpenOptionsCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const extensions::Extension* extension =
      input->profile_id == window_->site().profile_id()
          ? extensions::ExtensionRegistry::Get(window_->profile())
                ->enabled_extensions()
                .GetByID(input->id)
          : nullptr;
  if (!extension || input->id == kCluExtensionId) {
    std::move(callback).Run(ExtensionNotFound());
    return;
  }
  if (!extensions::OptionsPageInfo::HasOptionsPage(extension)) {
    std::move(callback).Run(RejectedResult("no-options"));
    return;
  }
  NavigateParams params(window_->browser(),
                        extensions::OptionsPageInfo::GetOptionsPage(extension),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::kNewPopup;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  params.window_features.bounds =
      gfx::Rect(0, 0, kOptionsWidth, kOptionsHeight);
  params.window_features.has_width = true;
  params.window_features.has_height = true;
  Navigate(&params);
  std::move(callback).Run(NullResult());
}

void CluExtensionsHandler::SetEnabled(mojom::ExtensionsSetEnabledInputPtr input,
                                      SetEnabledCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  Profile* profile = window_->profile();
  const extensions::Extension* extension =
      input->profile_id == window_->site().profile_id()
          ? InstalledExtension(profile, input->id)
          : nullptr;
  if (!extension) {
    std::move(callback).Run(ExtensionNotFound());
    return;
  }
  extensions::ManagementPolicy* policy =
      extensions::ExtensionSystem::Get(profile)->management_policy();
  if (policy && !policy->UserMayModifySettings(extension, nullptr)) {
    std::move(callback).Run(RejectedResult("policy"));
    return;
  }
  extensions::ExtensionRegistrar* registrar =
      extensions::ExtensionRegistrar::Get(profile);
  const bool enabled = extensions::ExtensionRegistry::Get(profile)
                           ->enabled_extensions()
                           .Contains(input->id);
  if (input->enabled && !enabled) {
    if (enabling_.contains(input->id)) {
      std::move(callback).Run(RejectedResult("busy"));
      return;
    }
    // The flow handles disabled and terminated extensions; a blocklisted one stays off.
    const extensions::ExtensionRegistry* registry =
        extensions::ExtensionRegistry::Get(profile);
    if (!registry->disabled_extensions().Contains(input->id) &&
        !registry->terminated_extensions().Contains(input->id)) {
      std::move(callback).Run(RejectedResult("not-enabled"));
      return;
    }
    const std::string id = input->id;
    auto request = std::make_unique<EnableRequest>(
        profile, id,
        base::BindOnce(
            [](base::WeakPtr<CluExtensionsHandler> handler, std::string id,
               SetEnabledCallback callback, bool done) {
              std::move(callback).Run(done ? NullResult()
                                           : RejectedResult("not-enabled"));
              if (handler) {
                handler->EnableDone(id);
              }
            },
            weak_factory_.GetWeakPtr(), id, std::move(callback)));
    EnableRequest* raw = request.get();
    enabling_[id] = std::move(request);
    raw->Start(
        window_->browser()->tab_strip_model()->GetActiveWebContents());
    return;
  }
  if (!input->enabled && enabled) {
    registrar->DisableExtension(
        input->id, {extensions::disable_reason::DISABLE_USER_ACTION});
  }
  std::move(callback).Run(NullResult());
}

void CluExtensionsHandler::EnableDone(const std::string& id) {
  // The flow may still be on the stack: free it once it has returned.
  auto it = enabling_.find(id);
  if (it != enabling_.end()) {
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(it->second));
    enabling_.erase(it);
  }
}

void CluExtensionsHandler::RunAction(mojom::ExtensionsRunActionInputPtr input,
                                     RunActionCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  std::move(callback).Run(
      window_->site().RunAction(input->id, RectOf(*input->anchor))
          ? NullResult()
          : ExtensionNotFound());
}

void CluExtensionsHandler::SetPinned(mojom::ExtensionsSetPinnedInputPtr input,
                                     SetPinnedCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // Pins belong to the profile; a private window changes none (Chromium's own toolbar refuses it too).
  if (window_->profile()->IsOffTheRecord()) {
    std::move(callback).Run(PrivateWindow());
    return;
  }
  if (!IsListedExtension(window_->profile(), input->id)) {
    std::move(callback).Run(ExtensionNotFound());
    return;
  }
  // No change (or a pin policy holds) is no error: the row already shows what it should.
  SetExtensionPinned(window_->profile(), input->id, input->pinned);
  std::move(callback).Run(NullResult());
}

void CluExtensionsHandler::OpenMenu(mojom::ExtensionsOpenMenuInputPtr input,
                                    OpenMenuCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  window_->site().OpenExtensionsMenu(RectOf(*input->anchor));
  std::move(callback).Run(NullResult());
}

namespace {

// The import summary's Reinstall into `profile`: an extension already on answers at once, else Chromium's Web Store
// prompt shows over a window of that profile on screen, or the calling window (onboarding's, the settings one's).
void InstallImported(base::WeakPtr<CluWindowState> window,
                     const std::string& id,
                     CluExtensionsHandler::InstallFromStoreCallback callback,
                     Profile* profile) {
  if (!window) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The import's profile is gone (a later Replace took it): the calling window's regular profile.
  if (!profile) {
    profile = window->profile()->GetOriginalProfile();
  }
  // Already on: the row turns to Installed without a prompt. A disabled or terminated one goes through the prompt,
  // which turns it back on (Chromium's install_chrome_app does the same).
  if (extensions::ExtensionRegistry::Get(profile)->GetExtensionById(
          id, extensions::ExtensionRegistry::ENABLED)) {
    std::move(callback).Run(NullResult());
    return;
  }
  CluWindowState* parent = window.get();
  for (CluWindowState* w : space_actions::AllWindows()) {
    if (w->profile() == profile && !w->hidden_by_switch() &&
        !w->browser()->IsDeleteScheduled()) {
      parent = w;
      break;
    }
  }
  // Bring the prompt's window in front of the settings window or onboarding's caller first.
  parent->browser()->GetWindow()->Activate();
  // The installer keeps itself alive from BeginInstall until it calls back.
  scoped_refptr<extensions::WebstoreInstallWithPrompt> installer =
      base::MakeRefCounted<extensions::WebstoreInstallWithPrompt>(
          id, profile, parent->browser()->GetWindow()->GetNativeWindow(),
          base::BindOnce(
              [](CluExtensionsHandler::InstallFromStoreCallback callback,
                 bool success, const std::string& error,
                 extensions::webstore_install::Result result) {
                if (success) {
                  std::move(callback).Run(NullResult());
                } else if (result == extensions::webstore_install::
                                         USER_CANCELLED ||
                           result == extensions::webstore_install::ABORTED) {
                  std::move(callback).Run(RejectedResult("cancelled"));
                } else {
                  std::move(callback).Run(RejectedResult("failed"));
                }
              },
              std::move(callback)));
  installer->BeginInstall();
}

}  // namespace

void CluExtensionsHandler::InstallFromStore(
    mojom::ExtensionsInstallFromStoreInputPtr input,
    InstallFromStoreCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // Only an extension the last sidebar import listed: the bridge is no prompt-on-demand install channel.
  CluImportService* imports = CluImportService::Get();
  const SidebarExtension* listed =
      imports ? imports->FindImportedExtension(input->id) : nullptr;
  if (!listed || input->id == kCluExtensionId) {
    std::move(callback).Run(ExtensionNotFound());
    return;
  }
  // Into the Rio profile the extension's source profile became, the one the user lands in, never the calling
  // window's: onboarding's Default window closes once setup moves to the import, and a private window (the settings
  // window can be bound to one) installs nothing.
  CluSpacesService* spaces = CluSpacesService::Get();
  if (listed->profile_id.empty() || !spaces) {
    InstallImported(window_, input->id, std::move(callback), nullptr);
    return;
  }
  spaces->LoadProfile(listed->profile_id,
                      base::BindOnce(&InstallImported, window_, input->id,
                                     std::move(callback)));
}

// tweaks.* -----------------------------------------------------------------------------------------------------------

CluTweaksHandler::CluTweaksHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluTweaksHandler::~CluTweaksHandler() = default;

void CluTweaksHandler::Get(mojom::TweaksGetInputPtr input,
                           GetCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/false);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  const SiteTweak* tweak = target->service->Tweak(target->site);
  std::move(callback).Run(ValueResult(
      tweak ? base::Value(SiteTweakToValue(*tweak)) : base::Value()));
}

void CluTweaksHandler::Save(mojom::TweaksSaveInputPtr input,
                            SaveCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  if (!IsTweakCodeSizeOk(input->css, input->js)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kBadPayload, "too-large"));
    return;
  }
  // The page shows what was saved, not the draft.
  if (CluSitePage* page = CluSitePage::Get(
          target->window_site->ContentsFor(input->tab_id))) {
    page->ClearDrafts();
  }
  std::optional<TweakControls> controls;
  if (input->controls) {
    controls = ControlsOf(*input->controls);
  }
  const CluSiteService::SaveResult result = target->service->SaveTweak(
      target->site, input->css, input->js, controls);
  if (!result.ok) {
    std::move(callback).Run(NotReady());
    return;
  }
  // A script that already ran cannot be taken back, so a JS change reloads the site's pages.
  if (result.enabled && result.old_js != input->js) {
    CluSitePage::ReloadSite(window_->profile(), target->site);
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::PreviewCss(mojom::TweaksPreviewCssInputPtr input,
                                  PreviewCssCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/false);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  if (!IsTweakCodeSizeOk(input->css, "")) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kBadPayload, "too-large"));
    return;
  }
  if (CluSitePage* page = CluSitePage::Get(
          target->window_site->ContentsFor(input->tab_id))) {
    page->PreviewCss(input->css);
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::PreviewControls(
    mojom::TweaksPreviewControlsInputPtr input,
    PreviewControlsCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/false);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  if (CluSitePage* page = CluSitePage::Get(
          target->window_site->ContentsFor(input->tab_id))) {
    page->PreviewControls(ControlsOf(*input->controls));
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::SetEnabled(mojom::TweaksSetEnabledInputPtr input,
                                  SetEnabledCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  // CSS and controls change live (CluSitePage::OnTweakChanged); JS needs a reload.
  const std::optional<bool> had_js =
      target->service->SetTweakEnabled(target->site, input->enabled);
  if (had_js.value_or(false)) {
    CluSitePage::ReloadSite(window_->profile(), target->site);
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::OpenEditor(mojom::TweaksOpenEditorInputPtr input,
                                  OpenEditorCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const std::string tab_id =
      input->tab_id ? *input->tab_id : window_->ActiveRowId();
  if (tab_id.empty()) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  base::expected<void, std::string> opened =
      window_->site().OpenTweakEditor(tab_id);
  if (!opened.has_value()) {
    std::move(callback).Run(opened.error() == "not-found"
                                ? TabNotFound()
                                : RejectedResult(opened.error()));
    return;
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::StartZap(mojom::TweaksStartZapInputPtr input,
                                StartZapCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  CluSitePage* page =
      CluSitePage::Get(target->window_site->ContentsFor(input->tab_id));
  if (!page) {
    std::move(callback).Run(RejectedResult("not-live"));
    return;
  }
  // Zap mode needs the page clickable: the Site controls popover (or any prompt) closes.
  target->window_site->ClosePrompts();
  std::move(callback).Run(page->StartZap() ? NullResult() : NoSite());
}

void CluTweaksHandler::StopZap(mojom::TweaksStopZapInputPtr input,
                               StopZapCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  CluWindowSite& w = window_->site();
  if (!w.HasRow(input->tab_id)) {
    std::move(callback).Run(TabNotFound());
    return;
  }
  if (CluSitePage* page = CluSitePage::Get(w.ContentsFor(input->tab_id))) {
    page->StopZap();
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::UndoZap(mojom::TweaksUndoZapInputPtr input,
                               UndoZapCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  if (CluSitePage* page = CluSitePage::Get(
          target->window_site->ContentsFor(input->tab_id));
      page && page->zap_active()) {
    page->UndoZap();
  } else {
    target->service->UndoZap(target->site);
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::RemoveZap(mojom::TweaksRemoveZapInputPtr input,
                                 RemoveZapCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  // An index past the end does nothing.
  target->service->RemoveZap(target->site,
                             static_cast<size_t>(std::max(0, input->index)));
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::ClearZaps(mojom::TweaksClearZapsInputPtr input,
                                 ClearZapsCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  auto target = Target(*window_, input->tab_id, /*write=*/true);
  if (!target.has_value()) {
    std::move(callback).Run(std::move(target).error());
    return;
  }
  target->service->ClearZaps(target->site);
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::List(mojom::TweaksListInputPtr input,
                            ListCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  CluWindowSite& w = window_->site();
  CluSiteService* service = w.service();
  if (input->profile_id != w.profile_id()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "not this window's profile"));
    return;
  }
  if (!service || !service->loaded()) {
    std::move(callback).Run(NotReady());
    return;
  }
  base::ListValue rows;
  for (const SiteTweak& tweak : service->ListTweaks()) {
    rows.Append(SiteTweakRowValue(tweak));
  }
  std::move(callback).Run(ValueResult(base::Value(std::move(rows))));
}

void CluTweaksHandler::SetEnabledForSite(
    mojom::TweaksSetEnabledForSiteInputPtr input,
    SetEnabledForSiteCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  CluWindowSite& w = window_->site();
  CluSiteService* service = w.service();
  if (input->profile_id != w.profile_id() || !service || !service->loaded() ||
      !service->Tweak(input->site)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no tweak for that site"));
    return;
  }
  if (!w.can_write()) {
    std::move(callback).Run(PrivateWindow());
    return;
  }
  if (service->SetTweakEnabled(input->site, input->enabled).value_or(false)) {
    CluSitePage::ReloadSite(window_->profile(), input->site);
  }
  std::move(callback).Run(NullResult());
}

void CluTweaksHandler::DeleteForSite(mojom::TweaksDeleteForSiteInputPtr input,
                                     DeleteForSiteCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  CluWindowSite& w = window_->site();
  CluSiteService* service = w.service();
  if (input->profile_id != w.profile_id()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "not this window's profile"));
    return;
  }
  if (!w.can_write()) {
    std::move(callback).Run(PrivateWindow());
    return;
  }
  if (!service || !service->loaded()) {
    std::move(callback).Run(NotReady());
    return;
  }
  // Its CSS leaves open pages at once; its JS with their reload.
  const std::optional<SiteTweak> old = service->DeleteTweak(input->site);
  if (old && old->enabled && HasJs(old->js)) {
    CluSitePage::ReloadSite(window_->profile(), input->site);
  }
  std::move(callback).Run(NullResult());
}

}  // namespace clu
