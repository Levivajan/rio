// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_data_handlers.h"

#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "clu/browser/data/clu_browsing_data.h"
#include "clu/browser/importer/clu_import_service.h"
#include "clu/browser/settings/clu_app_settings.h"
#include "clu/browser/settings/clu_settings_registry.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/theme/clu_theme_math.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/base_window.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/gurl.h"

namespace clu {

namespace {

constexpr char kClearDataSurface[] = "clear-data";
constexpr base::TimeDelta kClearedToast = base::Milliseconds(2000);

// Chromium's password page starts its CSV import when its router reaches settings?import=true, but on a direct load
// the route arrives before the page stamps its importer, and the page's assert fails. So the page opens on its
// settings section and, once loaded, steps its own router there, as the page's "Import passwords" link does. The
// script waits up to 5 s for the importer.
constexpr char16_t kStartPasswordImport[] = uR"((() => {
  const go = (n) => {
    const settings = document.querySelector('password-manager-app')
        ?.shadowRoot?.querySelector('settings-section');
    if (!settings?.shadowRoot?.querySelector('passwords-importer')) {
      if (n > 0) setTimeout(() => go(n - 1), 100);
      return;
    }
    history.pushState(null, '', '/settings?import=true');
    dispatchEvent(new PopStateEvent('popstate'));
  };
  go(50);
})();)";

bool IsPasswordPage(const GURL& url) {
  return url.SchemeIs("chrome") && url.host() == "password-manager";
}

// Runs kStartPasswordImport once the page has loaded, then deletes itself; it also goes when the page navigates
// elsewhere first or closes.
class PasswordImportStarter : public content::WebContentsObserver {
 public:
  static void Start(content::WebContents* contents) {
    auto* starter = new PasswordImportStarter(contents);
    if (!contents->IsLoading() && IsPasswordPage(contents->GetLastCommittedURL())) {
      starter->Run();
    }
  }

  void DidFinishLoad(content::RenderFrameHost* frame, const GURL& url) override {
    if (!frame->IsInPrimaryMainFrame()) {
      return;
    }
    if (IsPasswordPage(url)) {
      Run();
    } else {
      delete this;
    }
  }

  void WebContentsDestroyed() override { delete this; }

 private:
  explicit PasswordImportStarter(content::WebContents* contents)
      : content::WebContentsObserver(contents) {}
  ~PasswordImportStarter() override = default;

  void Run() {
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScript(
        kStartPasswordImport, base::NullCallback());
    delete this;
  }
};

mojom::CallResultPtr WindowGone() {
  return ErrorResult(mojom::ErrorCode::kRejected, "the window is closing");
}

mojom::CallResultPtr NoSpace() {
  return ErrorResult(mojom::ErrorCode::kNotFound, "no such space");
}

SpaceTheme ThemeOf(const mojom::SpaceTheme& in) {
  SpaceTheme theme;
  for (const mojom::ThemeColorPtr& color : in.colors) {
    theme.colors.push_back({color->h, color->s});
  }
  theme.intensity = in.intensity;
  theme.grain = in.grain;
  return theme;
}

// The guard checked the ranges; this checks again, as the browser trusts no payload (and a theme a later contract
// widens must still be one Clu can paint).
std::optional<SpaceTheme> CheckedTheme(const mojom::SpaceTheme& in) {
  return ParseSpaceTheme(base::Value(SpaceThemeValue(ThemeOf(in))));
}

// A space Clu knows. The private window's one unsaved space has no theme to keep.
bool SpaceExists(const std::string& space_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  return spaces && spaces->list().FindSpace(space_id);
}

ClearRange RangeOf(mojom::BrowsingDataClearInputRange range) {
  switch (range) {
    case mojom::BrowsingDataClearInputRange::kHour:
      return ClearRange::kHour;
    case mojom::BrowsingDataClearInputRange::kDay:
      return ClearRange::kDay;
    case mojom::BrowsingDataClearInputRange::kWeek:
      return ClearRange::kWeek;
    case mojom::BrowsingDataClearInputRange::kMonth:
      return ClearRange::kMonth;
    case mojom::BrowsingDataClearInputRange::kAll:
      return ClearRange::kAll;
  }
  return ClearRange::kHour;
}

mojom::CallResultPtr StartError(ImportStartError error) {
  return error == ImportStartError::kBusy
             ? RejectedResult("busy")
             : ErrorResult(mojom::ErrorCode::kNotFound, "nothing to import there");
}

base::Value OptionalString(const std::optional<std::string>& value) {
  return value ? base::Value(*value) : base::Value();
}

// The window on screen: `preferred` when it shows, else the first regular window that does.
CluWindowState* ShownWindow(CluWindowState* preferred) {
  auto shown = [](CluWindowState* w) {
    return w && !w->is_private() && !w->hidden_by_switch() &&
           !w->browser()->IsDeleteScheduled();
  };
  if (shown(preferred)) {
    return preferred;
  }
  for (CluWindowState* candidate : space_actions::AllWindows()) {
    if (shown(candidate)) {
      return candidate;
    }
  }
  return nullptr;
}

// The main space a sidebar import made while first-run setup showed: the setup screen stays on the window
// for the progress and the summary, and finishing setup moves the window there.
std::string& PendingImportSpace() {
  static base::NoDestructor<std::string> space_id;
  return *space_id;
}

// A new window on `space_id` (its profile loaded first): an import never leaves Rio with no window.
void OpenWindowOnSpace(const std::string& space_id) {
  CluSpacesService* spaces = CluSpacesService::Get();
  const SpaceEntry* entry = spaces ? spaces->list().FindSpace(space_id) : nullptr;
  if (!entry) {
    return;
  }
  spaces->LoadProfile(
      entry->profile_id,
      base::BindOnce(
          [](std::string space_id, Profile* profile) {
            // Another window may have shown meanwhile (a switch that was still loading).
            if (!profile || ShownWindow(nullptr)) {
              return;
            }
            BrowserWindowCreateParams params(*profile, /*from_user_gesture=*/true);
            params.should_trigger_session_restore = false;
            BrowserWindowInterface* browser = CreateBrowserWindow(std::move(params));
            CluWindowState* window = browser ? CluWindowState::GetOrCreate(browser) : nullptr;
            if (!window) {
              return;
            }
            window->SetInitialSpace(space_id);
            window->PrepareEmpty();
            browser->GetWindow()->Show();
          },
          space_id));
}

// The window on screen shows `space_id`. A space of another profile is in that profile's window, and the window it
// left closes instead of hiding (the import left the original window hidden, so no Rio window showed).
void ShowImportedSpace(CluWindowState* preferred, const std::string& space_id) {
  CluWindowState* target = ShownWindow(preferred);
  if (!target) {
    OpenWindowOnSpace(space_id);
  } else if (target->space_id() != space_id) {
    space_actions::SwitchWindowToSpace(target, space_id, /*close_source=*/true);
  }
}

// After a Replace that brought no main space (it stopped, or found nothing), a window opens on the first space left
// when none shows.
void EnsureAWindow() {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (ShownWindow(nullptr) || !spaces || spaces->list().spaces().empty()) {
    return;
  }
  OpenWindowOnSpace(spaces->list().spaces().front().id);
}

// import.run's answer for the sidebar import; the window then shows the space the source's first space became, so
// the user lands on their main space, one window and no hidden one. During first-run setup that waits
// until setup ends.
void AnswerSidebarImport(
    base::WeakPtr<CluWindowState> window,
    CluImportHandler::RunCallback callback,
    base::expected<SidebarImportResult, ImportStartError> result) {
  if (!result.has_value()) {
    EnsureAWindow();
    std::move(callback).Run(StartError(result.error()));
    return;
  }
  const SidebarImportResult& r = result.value();
  CluAppSettings* settings = CluAppSettings::Get();
  if (!r.main_space_id.empty() && settings && settings->onboarding_show()) {
    PendingImportSpace() = r.main_space_id;
    // Setup goes on in a window on screen; a Replace that closed it opens one on the main space.
    OpenWindowOnSpace(r.main_space_id);
  } else if (!r.main_space_id.empty()) {
    ShowImportedSpace(window.get(), r.main_space_id);
  } else {
    EnsureAWindow();
  }
  base::ListValue extensions;
  for (const SidebarExtension& e : r.extensions) {
    extensions.Append(base::DictValue().Set("id", e.id).Set("name", e.name));
  }
  std::move(callback).Run(ValueResult(base::Value(
      base::DictValue()
          .Set("bookmarks", r.bookmarks)
          .Set("searchEngine", OptionalString(r.search_engine))
          .Set("extensions", std::move(extensions))
          .Set("profiles", r.profiles)
          .Set("spaces", r.spaces)
          .Set("pinned", r.pinned)
          .Set("today", r.today)
          .Set("favorites", r.favorites)
          .Set("history", r.history)
          .Set("passwords", r.passwords)
          .Set("cookies", r.cookies)
          .Set("secretsDenied", r.secrets_denied)
          .Set("siteStorage", r.site_storage)
          .Set("permissions", r.permissions)
          .Set("addresses", r.addresses)
          .Set("secretsSkipped",
               r.secrets_skipped.undecryptable +
                   r.secrets_skipped.bad_rows +
                   r.secrets_skipped.over_cap)
          .Set("skipped",
               base::DictValue()
                   .Set("unsupported", r.skipped.unsupported)
                   .Set("missing", r.skipped.missing)
                   .Set("cycles", r.skipped.cycles)
                   .Set("badUrls", r.skipped.bad_urls)
                   .Set("overCap", r.skipped.over_cap)
                   .Set("duplicates", r.skipped.duplicates)
                   .Set("historyFiles", r.skipped.history_files))
          .Set("error", OptionalString(r.error)))));
}

// Re-import: each profile in `ids` goes with its spaces (space_actions::DeleteSpace() moves the windows
// that show one), then the profile itself; a profile whose space the list can't lose (the last one) stays. Then
// `done`.
void RemoveImportedProfiles(std::vector<std::string> ids, base::OnceClosure done) {
  CluSpacesService* spaces = CluSpacesService::Get();
  if (ids.empty() || !spaces) {
    std::move(done).Run();
    return;
  }
  const std::string id = ids.back();
  std::string space_id;
  for (const SpaceEntry& space : spaces->list().spaces()) {
    if (space.profile_id == id) {
      space_id = space.id;
      break;
    }
  }
  if (!space_id.empty()) {
    space_actions::DeleteSpace(
        space_id,
        base::BindOnce(
            [](std::vector<std::string> ids, base::OnceClosure done,
               base::expected<void, SpacesError> deleted) {
              if (!deleted.has_value()) {
                ids.pop_back();
              }
              RemoveImportedProfiles(std::move(ids), std::move(done));
            },
            std::move(ids), std::move(done)));
    return;
  }
  ids.pop_back();
  // The user said yes to the Replace and its windows moved off (hidden), where no one could answer a page's prompt.
  std::ignore = spaces->DeleteProfile(id, /*skip_beforeunload=*/true);
  RemoveImportedProfiles(std::move(ids), std::move(done));
}

}  // namespace

// history.open -------------------------------------------------------------------

CluHistoryHandler::CluHistoryHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluHistoryHandler::~CluHistoryHandler() = default;

void CluHistoryHandler::Open(OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // Cmd+Y focus-or-open in the window's space; a private window keeps no history and lets Chromium open it in a
  // normal window, as Chrome does.
  if (!window_->RunCommand("history.open")) {
    chrome::ExecuteCommand(window_->browser(), IDC_SHOW_HISTORY);
  }
  std::move(callback).Run(NullResult());
}

// browsingData.* -----------------------------------------------------------------

CluBrowsingDataHandler::CluBrowsingDataHandler(
    base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluBrowsingDataHandler::~CluBrowsingDataHandler() = default;

void CluBrowsingDataHandler::Open(mojom::BrowsingDataOpenInputPtr input,
                                  OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The dialog picks the profile: the one asked for (Settings > Privacy names its profile), else the window's.
  window_->OpenPrompt(
      kClearDataSurface,
      base::DictValue().Set("profileId", input->profile_id
                                             ? base::Value(*input->profile_id)
                                             : base::Value(window_->state_profile_id())));
  std::move(callback).Run(NullResult());
}

void CluBrowsingDataHandler::Clear(mojom::BrowsingDataClearInputPtr input,
                                   ClearCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  ClearTypes types;
  types.history = input->types->history;
  types.cookies = input->types->cookies;
  types.cache = input->types->cache;
  types.downloads = input->types->downloads;
  types.permissions = input->types->permissions;
  types.archive = input->types->archive;
  const ClearPlan plan = PlanClear(RangeOf(input->range), types, base::Time::Now());

  auto run = [](base::WeakPtr<CluBrowsingDataHandler> self, ClearPlan plan,
                ClearCallback callback, Profile* profile) {
    if (!self || !profile) {
      std::move(callback).Run(
          ErrorResult(mojom::ErrorCode::kNotFound, "no such profile"));
      return;
    }
    RunClearPlan(
        profile, plan,
        base::BindOnce(
            [](base::WeakPtr<CluBrowsingDataHandler> self,
               ClearCallback callback) {
              if (self && self->window_) {
                self->window_->ShowToast("Browsing data cleared", std::nullopt,
                                         kClearedToast);
              }
              std::move(callback).Run(NullResult());
            },
            self, std::move(callback)));
  };

  // The window's own profile (its private profile included), else another Clu profile, loaded for the clear.
  if (input->profile_id == window_->state_profile_id()) {
    run(weak_ptr_factory_.GetWeakPtr(), plan, std::move(callback),
        window_->profile());
    return;
  }
  CluSpacesService* spaces = CluSpacesService::Get();
  if (!spaces || !spaces->list().FindProfile(input->profile_id)) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kNotFound, "no such profile"));
    return;
  }
  spaces->LoadProfile(input->profile_id,
                      base::BindOnce(run, weak_ptr_factory_.GetWeakPtr(), plan,
                                     std::move(callback)));
}

// import.* -----------------------------------------------------------------------

CluImportHandler::CluImportHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluImportHandler::~CluImportHandler() = default;

void CluImportHandler::Detect(DetectCallback callback) {
  CluImportService* service = CluImportService::Get();
  if (!window_ || !service) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // The settings tab's space is the default target; a private window has none Clu keeps.
  const std::optional<std::string> current =
      window_->is_private() ? std::nullopt
                            : std::optional<std::string>(window_->space_id());
  service->Detect(base::BindOnce(
      [](std::optional<std::string> current, DetectCallback callback,
         ImportDetection detection) {
        base::ListValue profiles;
        for (const ChromeProfileInfo& p : detection.chrome) {
          profiles.Append(base::DictValue()
                              .Set("dir", p.dir)
                              .Set("name", p.name)
                              .Set("hasBookmarks", p.has_bookmarks)
                              .Set("hasHistory", p.has_history));
        }
        std::move(callback).Run(ValueResult(base::Value(
            base::DictValue()
                .Set("chrome",
                     base::DictValue().Set("profiles", std::move(profiles)))
                .Set("arc", base::DictValue()
                                .Set("found", detection.sidebar)
                                .Set("previous", detection.previous))
                .Set("currentSpaceId", OptionalString(current)))));
      },
      current, std::move(callback)));
}

void CluImportHandler::Run(mojom::ImportRunInputPtr input,
                           RunCallback callback) {
  CluImportService* service = CluImportService::Get();
  if (!window_ || !service) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (input->is_chrome()) {
    const mojom::ImportRunInputChrome& chrome = *input->get_chrome();
    ChromeImportRequest request;
    request.profile_dir = chrome.profile_dir;
    request.bookmarks = chrome.bookmarks;
    request.history = chrome.history;
    request.passwords = chrome.passwords;
    request.cookies = chrome.cookies;
    request.autofill = chrome.autofill;
    request.search_engine = chrome.search_engine;
    request.target_space_id = chrome.target_space_id;
    service->RunChrome(
        std::move(request),
        base::BindOnce(
            [](RunCallback callback,
               base::expected<ChromeImportResult, ImportStartError> result) {
              if (!result.has_value()) {
                std::move(callback).Run(StartError(result.error()));
                return;
              }
              const ChromeImportResult& r = result.value();
              std::move(callback).Run(ValueResult(base::Value(
                  base::DictValue()
                      .Set("bookmarks", r.bookmarks)
                      .Set("history", r.history)
                      .Set("passwords", r.passwords)
                      .Set("cookies", r.cookies)
                      .Set("secretsDenied", r.secrets_denied)
                      .Set("siteStorage", r.site_storage)
                      .Set("permissions", r.permissions)
                      .Set("addresses", r.addresses)
                      .Set("searchEngine", OptionalString(r.search_engine))
                      .Set("skipped", r.skipped)
                      .Set("error", OptionalString(r.error)))));
            },
            std::move(callback)));
    return;
  }
  const mojom::ImportRunInputArc& arc = *input->get_arc();
  if (service->running()) {
    std::move(callback).Run(StartError(ImportStartError::kBusy));
    return;
  }
  // Items left out of the request come over (everything is ticked).
  SidebarImportOptions options{.passwords = arc.passwords,
                               .cookies = arc.cookies,
                               .favorites = arc.favorites.value_or(true),
                               .pinned = arc.pinned.value_or(true),
                               .today = arc.today.value_or(true),
                               .history = arc.history.value_or(true),
                               .bookmarks = arc.bookmarks.value_or(true),
                               .search_engine = arc.search_engine.value_or(true),
                               .extensions = arc.extensions.value_or(true),
                               .autofill = arc.autofill.value_or(true),
                               .replace = arc.replace.value_or(false)};
  // The run outlives this handler when a replaced profile's window (this one, say) closes on the way.
  base::OnceClosure run = base::BindOnce(
      [](SidebarImportOptions options, base::WeakPtr<CluWindowState> window,
         RunCallback callback) {
        CluImportService* service = CluImportService::Get();
        if (!service) {
          std::move(callback).Run(WindowGone());
          return;
        }
        service->RunSidebar(
            options, base::BindOnce(&AnswerSidebarImport, std::move(window),
                                    std::move(callback)));
      },
      options, window_, std::move(callback));
  if (!options.replace) {
    std::move(run).Run();
    return;
  }
  // The user said yes to replacing the previous import, so its profiles and the "Imported N" ones a broken
  // run left go first, with their spaces, and the map that found them is forgotten.
  std::vector<std::string> previous = service->PreviousSidebarProfiles();
  service->ForgetSidebarImport();
  // A window of a profile that goes moves first to a space that stays, so it never chains through spaces that go too.
  // The marks outlive a close a quit cuts short: the next launch removes what is left.
  if (CluSpacesService* spaces = CluSpacesService::Get()) {
    spaces->MarkProfilesForRemoval(previous);
    const std::set<std::string> going(previous.begin(), previous.end());
    const SpaceEntry* stays = nullptr;
    for (const SpaceEntry& space : spaces->list().spaces()) {
      if (!going.contains(space.profile_id)) {
        stays = &space;
        break;
      }
    }
    const std::string stays_id = stays ? stays->id : std::string();
    for (CluWindowState* w : space_actions::AllWindows()) {
      // By the window's profile, not its space: a window opened from outside may show none of its profile's spaces.
      if (!stays_id.empty() && !w->is_private() && !w->hidden_by_switch() &&
          going.contains(spaces->ProfileIdOf(w->profile()))) {
        space_actions::SwitchWindowToSpace(w, stays_id, /*close_source=*/false);
      }
    }
  }
  RemoveImportedProfiles(std::move(previous), std::move(run));
}

void CluImportHandler::OpenPasswords(OpenPasswordsCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // Chromium's password manager runs its CSV import (UrlParam.START_IMPORT in chrome/browser/resources/
  // password_manager): its own file picker, conflict dialog and device check follow.
  if (!window_->model()) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kNotImplemented,
                                        "a private window keeps no rows"));
    return;
  }
  tabs::TabInterface* tab = nullptr;
  if (window_->OpenInternalPage(GURL("chrome://password-manager/settings"))) {
    tab = window_->FindTab(window_->ActiveRowId());
  }
  if (!tab || !tab->GetContents()) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kRejected,
                                        "the password import didn't open"));
    return;
  }
  PasswordImportStarter::Start(tab->GetContents());
  std::move(callback).Run(NullResult());
}

// theme.* ------------------------------------------------------------------------

CluThemeHandler::CluThemeHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluThemeHandler::~CluThemeHandler() = default;

void CluThemeHandler::SetMode(mojom::ThemeSetModeInputPtr input,
                              SetModeCallback callback) {
  CluAppSettings* settings = CluAppSettings::Get();
  if (!window_ || !settings) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const char* mode = input->mode == mojom::ThemeSetModeInputMode::kLight ? "light"
                     : input->mode == mojom::ThemeSetModeInputMode::kDark
                         ? "dark"
                         : "auto";
  settings->SetGlobal(kAppearanceModeKey, base::Value(mode));
  std::move(callback).Run(NullResult());
}

void CluThemeHandler::PreviewSpaceTheme(
    mojom::ThemePreviewSpaceThemeInputPtr input,
    PreviewSpaceThemeCallback callback) {
  CluAppSettings* settings = CluAppSettings::Get();
  const std::optional<SpaceTheme> theme = CheckedTheme(*input->theme);
  if (!window_ || !settings) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!theme) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kBadPayload));
    return;
  }
  if (!SpaceExists(input->space_id)) {
    std::move(callback).Run(NoSpace());
    return;
  }
  settings->PreviewSpaceTheme(input->space_id, *theme);
  std::move(callback).Run(NullResult());
}

void CluThemeHandler::SetSpaceTheme(mojom::ThemeSetSpaceThemeInputPtr input,
                                    SetSpaceThemeCallback callback) {
  CluAppSettings* settings = CluAppSettings::Get();
  const std::optional<SpaceTheme> theme = CheckedTheme(*input->theme);
  if (!window_ || !settings) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!theme) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kBadPayload));
    return;
  }
  if (!SpaceExists(input->space_id)) {
    std::move(callback).Run(NoSpace());
    return;
  }
  settings->SetSpaceTheme(input->space_id, *theme);
  std::move(callback).Run(NullResult());
}

void CluThemeHandler::ResetSpaceTheme(mojom::ThemeResetSpaceThemeInputPtr input,
                                      ResetSpaceThemeCallback callback) {
  CluAppSettings* settings = CluAppSettings::Get();
  if (!window_ || !settings) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!SpaceExists(input->space_id)) {
    std::move(callback).Run(NoSpace());
    return;
  }
  settings->ResetSpaceTheme(input->space_id);
  std::move(callback).Run(NullResult());
}

void CluThemeHandler::OpenPicker(mojom::ThemeOpenPickerInputPtr input,
                                 OpenPickerCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  if (!SpaceExists(input->space_id) || window_->is_private()) {
    std::move(callback).Run(NoSpace());
    return;
  }
  window_->OpenThemePicker(
      input->space_id,
      gfx::ToEnclosingRect(gfx::RectF(input->anchor->x, input->anchor->y,
                                      input->anchor->width,
                                      input->anchor->height)));
  std::move(callback).Run(NullResult());
}

// onboarding.complete --------------------------------------------------------------

CluOnboardingHandler::CluOnboardingHandler(base::WeakPtr<CluWindowState> window)
    : window_(std::move(window)) {}
CluOnboardingHandler::~CluOnboardingHandler() = default;

void CluOnboardingHandler::Complete(mojom::OnboardingCompleteInputPtr input,
                                    CompleteCallback callback) {
  CluAppSettings* settings = CluAppSettings::Get();
  if (!window_ || !settings) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // A no-op once it is hidden: a second window's Skip or a double click.
  settings->CompleteOnboarding(input->skipped);
  // An import during setup moves the window to its main space now.
  const std::string space_id = std::exchange(PendingImportSpace(), std::string());
  if (!space_id.empty() && SpaceExists(space_id)) {
    ShowImportedSpace(window_.get(), space_id);
  }
  std::move(callback).Run(NullResult());
}

}  // namespace clu
