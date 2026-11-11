// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/clu_command_bar.h"

#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "clu/browser/command/clu_history_index.h"
#include "clu/browser/command/clu_suggest_rules.h"
#include "clu/browser/commands/clu_command_routes.h"
#include "clu/browser/model/clu_model_service.h"
#include "clu/browser/model/clu_tab_node.h"
#include "clu/browser/spaces/clu_space_list.h"
#include "clu/browser/spaces/clu_space_rules.h"
#include "clu/browser/spaces/clu_spaces_service.h"
#include "clu/browser/ui/webui/bridge/call_result.h"
#include "clu/browser/ui/webui/clu_space_actions.h"
#include "clu/browser/ui/webui/clu_state_builder.h"
#include "clu/browser/ui/webui/clu_url_policy.h"
#include "clu/browser/ui/webui/clu_window_multitask.h"
#include "clu/browser/ui/webui/clu_window_state.h"
#include "components/history/core/common/pref_names.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/search_engine_type.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace clu {

namespace {

// The controller stops itself after its stop timer (1.5 s by default); this answers a request even if it never does.
constexpr base::TimeDelta kSuggestTimeout = base::Seconds(2);
// The longest engine name state carries, and the most engine rows the bar offers.
constexpr size_t kMaxEngineName = 64;
constexpr size_t kMaxEngineRows = 20;
// A provider row's title and its space name part (src/shared/command/actions.ts MAX_ACTION_TITLE).
constexpr size_t kMaxActionTitle = 120;
constexpr size_t kMaxSpaceNameInRow = 80;
// A pinned index past the end of any list: the move lands at the end (the list clamps it).
constexpr int kEndOfList = 1 << 20;

// `text` on one line, cut at `max` UTF-16 units without splitting a surrogate pair.
std::string RowTitle(std::u16string_view text, size_t max) {
  std::u16string line;
  for (char16_t c : text) {
    line.push_back(base::IsUnicodeControl(c) ? u' ' : c);
  }
  std::u16string_view trimmed = base::TrimWhitespace(line, base::TRIM_ALL);
  if (trimmed.size() > max) {
    size_t cut = max;
    if ((trimmed[cut - 1] & 0xFC00) == 0xD800) {
      --cut;
    }
    trimmed = trimmed.substr(0, cut);
  }
  return base::UTF16ToUTF8(trimmed);
}

base::DictValue ProviderRow(std::string_view provider_id,
                            std::string_view token,
                            const std::string& title,
                            const std::optional<std::string>& subtitle) {
  base::DictValue row = base::DictValue()
                            .Set("id", provider_id)
                            .Set("providerId", provider_id)
                            .Set("token", token)
                            .Set("title", title);
  if (subtitle) {
    row.Set("subtitle", *subtitle);
  }
  return row;
}

const char* EngineId(SearchEngineType type) {
  switch (type) {
    case SEARCH_ENGINE_BING:
      return "bing";
    case SEARCH_ENGINE_DUCKDUCKGO:
      return "duckduckgo";
    case SEARCH_ENGINE_ECOSIA:
      return "ecosia";
    case SEARCH_ENGINE_BRAVE:
      return "brave";
    default:
      return "google";
  }
}

}  // namespace

std::optional<std::u16string> SuggestionText(const AutocompleteMatch& match,
                                             std::u16string_view default_keyword) {
  // Only the engine's own strings: the typed query itself and past
  // searches are rows the bar makes on its own.
  if (match.type != omnibox::AutocompleteMatchType::kSearchSuggest &&
      match.type != omnibox::AutocompleteMatchType::kSearchSuggestEntity &&
      match.type != omnibox::AutocompleteMatchType::kSearchSuggestTail &&
      match.type != omnibox::AutocompleteMatchType::kSearchSuggestPersonalized) {
    return std::nullopt;
  }
  if (default_keyword.empty() || match.keyword != default_keyword) {
    return std::nullopt;
  }
  if (match.search_terms_args && !match.search_terms_args->search_terms.empty()) {
    return match.search_terms_args->search_terms;
  }
  return match.fill_into_edit;
}

namespace {

// The profile's engines the bar offers: Chromium's prepopulated ones and the current default. CanMakeDefault()
// answers false for the engine that already is the default, so the current one is listed on its own.
std::vector<TemplateURL*> OfferedEngines(TemplateURLService* service) {
  std::vector<TemplateURL*> out;
  if (!service || !service->loaded()) {
    return out;
  }
  const TemplateURL* current = service->GetDefaultSearchProvider();
  for (TemplateURL* engine : service->GetTemplateURLs()) {
    if (out.size() == kMaxEngineRows) {
      break;
    }
    if (engine == current ||
        (engine->type() == TemplateURL::NORMAL &&
         engine->prepopulate_id() > 0 && service->CanMakeDefault(engine))) {
      out.push_back(engine);
    }
  }
  return out;
}

mojom::CallResultPtr WindowGone() {
  return ErrorResult(mojom::ErrorCode::kRejected, "the window is closing");
}

// Output<'command.execute'>: { ok: boolean; error?: 'stale' | 'invalid' | 'blocked-url' }.
mojom::CallResultPtr ExecuteOk() {
  return ValueResult(base::Value(base::DictValue().Set("ok", true)));
}

mojom::CallResultPtr ExecuteError(std::string_view error) {
  return ValueResult(
      base::Value(base::DictValue().Set("ok", false).Set("error", error)));
}

ui::PageTransition TypedTransition() {
  return ui::PageTransitionFromInt(ui::PAGE_TRANSITION_TYPED |
                                   ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
}

}  // namespace

CluCommandHandler::CluCommandHandler(base::WeakPtr<CluWindowState> window,
                                     Profile* profile)
    : window_(std::move(window)), profile_(profile) {}

CluCommandHandler::~CluCommandHandler() {
  // A pending request still gets its answer: a dropped mojo callback on a live pipe is an error.
  FinishSuggest(/*with_results=*/false);
}

void CluCommandHandler::Open(mojom::CommandOpenInputPtr input,
                             OpenCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  switch (input->mode) {
    case mojom::CommandOpenInputMode::kNewTab:
      window_->OpenCommandBar(CommandBarMode::kNewTab);
      break;
    case mojom::CommandOpenInputMode::kEditUrl:
      window_->OpenCommandBar(CommandBarMode::kEditUrl);
      break;
    case mojom::CommandOpenInputMode::kMini:
      // Only a Mini window has a page for the bar to load into.
      if (window_->shell().is_mini()) {
        window_->OpenCommandBar(CommandBarMode::kMini);
      }
      break;
    case mojom::CommandOpenInputMode::kSplitAdd:
      // The chosen result opens as a pane beside the page on screen (05
      // "Shortcuts").
      window_->OpenCommandBar(CommandBarMode::kSplitAdd);
      break;
  }
  std::move(callback).Run(NullResult());
}

void CluCommandHandler::Close(mojom::CommandCloseInputPtr input,
                              CloseCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // A stale session id closes nothing.
  const std::optional<CommandSessionSnapshot>& session =
      window_->command_session();
  if (session && session->session_id == input->session_id) {
    window_->CloseCommandBar();
  }
  std::move(callback).Run(NullResult());
}

void CluCommandHandler::Execute(mojom::CommandExecuteInputPtr input,
                                ExecuteCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const std::optional<CommandSessionSnapshot>& current =
      window_->command_session();
  if (!current || current->session_id != input->session_id) {
    std::move(callback).Run(ExecuteError("stale"));
    return;
  }
  // Closing the bar ends the session, so keep a copy.
  const CommandSessionSnapshot session = *current;
  // Option+Enter, or any result of the split-add bar: the page opens beside the
  // one on screen (M6).
  const bool split =
      input->disposition == mojom::CommandExecuteInputDisposition::kSplit ||
      session.mode == "split-add";
  const bool new_tab =
      input->disposition == mojom::CommandExecuteInputDisposition::kNewTab;
  const mojom::CommandResult& result = *input->result;

  // The overlay's classification is never trusted: every URL is checked again here.
  GURL url;
  if (result.is_navigate()) {
    // rio://settings and the other rio:// aliases load their chrome:// page (M8).
    url = ResolveCluUrl(GURL(result.get_navigate()->url));
    if (!IsTypedUrlAllowed(url)) {
      // The bar stays open on its text; the toast says why nothing happened.
      window_->ShowToast("This address can't be opened", std::nullopt,
                         base::Milliseconds(1500));
      std::move(callback).Run(ExecuteError("blocked-url"));
      return;
    }
  } else if (result.is_search()) {
    const std::string query(base::TrimWhitespaceASCII(
        result.get_search()->query, base::TRIM_ALL));
    // The profile's engine, read now: the row's engine_id only labels it.
    url = query.empty() ? GURL() : SearchUrl(query);
    if (!url.is_valid()) {
      std::move(callback).Run(ExecuteError("invalid"));
      return;
    }
  } else if (result.is_history()) {
    url = GURL(result.get_history()->url);
    if (!IsWebUrl(url)) {
      std::move(callback).Run(ExecuteError("blocked-url"));
      return;
    }
  } else if (result.is_tab()) {
    const mojom::CommandResultTab& row = *result.get_tab();
    if (split && window_->model() && window_->model()->tree().Get(row.tab_id)) {
      window_->CloseCommandBar();
      const bool ok =
          window_->multitask().OpenRowInSplit(row.tab_id).has_value();
      window_->FocusActivePage();
      std::move(callback).Run(ok ? ExecuteOk() : ExecuteError("invalid"));
      return;
    }
    if (!new_tab) {
      // A row without a page, or one another window shows, switches too: ActivateRow() loads or moves its page.
      if (window_->model() && window_->model()->tree().Get(row.tab_id)) {
        window_->CloseCommandBar();
        if (window_->ActivateRow(row.tab_id)) {
          window_->FocusActivePage();
          std::move(callback).Run(ExecuteOk());
          return;
        }
      } else if (tabs::TabInterface* tab = window_->FindTab(row.tab_id)) {
        window_->CloseCommandBar();
        window_->ActivateTab(tab);
        window_->FocusActivePage();
        std::move(callback).Run(ExecuteOk());
        return;
      }
    }
    // The tab closed since the list rendered: its URL opens in a new tab instead.
    url = ResolveCluUrl(GURL(row.url));
    if (!IsTypedUrlAllowed(url)) {
      std::move(callback).Run(ExecuteError("blocked-url"));
      return;
    }
    window_->CloseCommandBar();
    OpenUrl(session, url, /*new_tab=*/true);
    window_->FocusActivePage();
    std::move(callback).Run(ExecuteOk());
    return;
  } else if (result.is_reload()) {
    tabs::TabInterface* target =
        session.target_tab_id ? window_->FindTab(*session.target_tab_id)
                              : nullptr;
    if (!target || result.get_reload()->tab_id != *session.target_tab_id) {
      std::move(callback).Run(ExecuteError("invalid"));
      return;
    }
    window_->CloseCommandBar();
    window_->Reload(target, /*bypass_cache=*/false);
    window_->FocusActivePage();
    std::move(callback).Run(ExecuteOk());
    return;
  } else if (result.is_command()) {
    // Only palette commands this window can run now: what command.actions listed.
    const std::string id = result.get_command()->id;
    const CommandSpec* spec = FindCommand(id);
    if (!spec || !spec->palette || spec->variant == CommandVariant::kMini ||
        !window_->CanRunCommand(id)) {
      std::move(callback).Run(ExecuteError("invalid"));
      return;
    }
    // Page commands (Reload Page, Find in Page) act on the page under the bar, as they would from the menu.
    window_->CloseCommandBar();
    window_->RunCommand(id);
    std::move(callback).Run(ExecuteOk());
    return;
  } else if (result.is_provided()) {
    // RunProvided() closes the bar once the token checks out; a stale token keeps it open.
    const mojom::CommandResultProvided& row = *result.get_provided();
    std::move(callback).Run(RunProvided(row.provider_id, row.token)
                                ? ExecuteOk()
                                : ExecuteError("invalid"));
    return;
  } else {
    // Any other action kind is invalid: palette commands and providers cover those rows.
    std::move(callback).Run(ExecuteError("invalid"));
    return;
  }

  window_->CloseCommandBar();
  if (split && !window_->shell().is_mini()) {
    std::ignore = window_->multitask().OpenUrlInSplit(url);
  } else {
    OpenUrl(session, url, new_tab);
  }
  // Like an address bar, a page result leaves focus on the page it loads in.
  window_->FocusActivePage();
  std::move(callback).Run(ExecuteOk());
}

void CluCommandHandler::Suggest(mojom::CommandSuggestInputPtr input,
                                SuggestCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  // Only the newest request gets matches; the bar drops older answers anyway.
  FinishSuggest(/*with_results=*/false);
  pending_request_id_ = input->request_id;
  pending_session_id_ = input->session_id;
  pending_callback_ = std::move(callback);

  const std::optional<CommandSessionSnapshot>& session =
      window_->command_session();
  // Input that must not leave the machine (a scheme, credentials, an email address) asks the engine nothing.
  if (!session || session->session_id != input->session_id ||
      !ShouldSuggest(base::UTF8ToUTF16(input->query))) {
    FinishSuggest(/*with_results=*/false);
    return;
  }
  if (!controller_) {
    controller_ = std::make_unique<AutocompleteController>(
        std::make_unique<ChromeAutocompleteProviderClient>(profile_),
        AutocompleteControllerConfig{
            .provider_types =
                AutocompleteClassifier::DefaultOmniboxProviders()});
    controller_observation_.Observe(controller_.get());
  }
  AutocompleteInput autocomplete_input(
      base::UTF8ToUTF16(input->query), metrics::OmniboxEventProto::OTHER,
      ChromeAutocompleteSchemeClassifier(profile_));
  autocomplete_input.set_prevent_inline_autocomplete(true);
  // "bing.com harbor" is a search for those words on the default engine, never keyword mode on another engine.
  autocomplete_input.set_allow_exact_keyword_match(false);
  controller_->Start(autocomplete_input);
  if (controller_->done()) {
    FinishSuggest(/*with_results=*/true);
    return;
  }
  if (pending_callback_) {
    suggest_timeout_.Start(
        FROM_HERE, kSuggestTimeout,
        base::BindOnce(&CluCommandHandler::FinishSuggest,
                       base::Unretained(this), /*with_results=*/true));
  }
}

void CluCommandHandler::OnResultChanged(AutocompleteController* controller,
                                        bool default_match_changed) {
  if (pending_callback_ && controller->done()) {
    FinishSuggest(/*with_results=*/true);
  }
}

void CluCommandHandler::FinishSuggest(bool with_results) {
  if (!pending_callback_) {
    return;
  }
  suggest_timeout_.Stop();
  base::ListValue items;
  const bool session_live =
      window_ && window_->command_session() &&
      window_->command_session()->session_id == pending_session_id_;
  if (with_results && controller_ && session_live) {
    std::vector<std::u16string> raw;
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(profile_);
    const TemplateURL* engine =
        service ? service->GetDefaultSearchProvider() : nullptr;
    const std::u16string keyword = engine ? engine->keyword() : u"";
    for (const AutocompleteMatch& match : controller_->result()) {
      if (std::optional<std::u16string> text = SuggestionText(match, keyword)) {
        raw.push_back(std::move(*text));
      }
    }
    for (std::u16string& text : CleanSuggestions(raw)) {
      items.Append(std::move(text));
    }
  }
  const double request_id = pending_request_id_.value_or(0);
  pending_request_id_.reset();
  pending_session_id_.clear();
  std::move(pending_callback_)
      .Run(ValueResult(base::Value(base::DictValue()
                                       .Set("requestId", request_id)
                                       .Set("items", std::move(items)))));
}

GURL CluCommandHandler::SearchUrl(const std::string& query) {
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  const TemplateURL* engine =
      service ? service->GetDefaultSearchProvider() : nullptr;
  if (!engine) {
    return GURL();
  }
  return GURL(engine->url_ref().ReplaceSearchTerms(
      TemplateURLRef::SearchTermsArgs(base::UTF8ToUTF16(query)),
      service->search_terms_data()));
}

void CluCommandHandler::OpenUrl(const CommandSessionSnapshot& session,
                                const GURL& url,
                                bool new_tab) {
  tabs::TabInterface* target =
      session.mode == "edit-url" && session.target_tab_id
          ? window_->FindTab(*session.target_tab_id)
          : nullptr;
  // A Mini window's bar loads its one page (Cmd+Option+N, 05 "Mini window").
  if (session.mode == "mini" && window_->shell().is_mini()) {
    target = window_->ActiveTab();
    new_tab = false;
  }
  if (!new_tab && target) {
    content::WebContents* contents = target->GetContents();
    // Enter on the unchanged URL reloads (04-command F2).
    if (contents->GetVisibleURL() == url) {
      window_->Reload(target, /*bypass_cache=*/false);
      return;
    }
    NavigateParams params(window_->browser(), url, TypedTransition());
    params.disposition = WindowOpenDisposition::kCurrentTab;
    params.source_contents = contents;
    Navigate(&params);
    return;
  }
  NavigateParams params(window_->browser(), url, TypedTransition());
  params.disposition = WindowOpenDisposition::kNewForegroundTab;
  Navigate(&params);
}

bool SetDefaultSearchEngine(Profile* profile, std::string_view engine_id) {
  TemplateURLService* service =
      profile ? TemplateURLServiceFactory::GetForProfile(profile) : nullptr;
  for (TemplateURL* engine : OfferedEngines(service)) {
    const SearchEngineType type =
        engine->GetEngineType(service->search_terms_data());
    // EngineId() reads every other engine as google, so google itself must be the Google engine.
    if (engine_id == "google" ? type == SEARCH_ENGINE_GOOGLE
                              : engine_id == EngineId(type)) {
      service->SetUserSelectedDefaultSearchProvider(engine);
      return true;
    }
  }
  return false;
}

SearchEngineInfo DefaultSearchEngine(Profile* profile) {
  SearchEngineInfo info;
  TemplateURLService* service =
      profile ? TemplateURLServiceFactory::GetForProfile(profile) : nullptr;
  const TemplateURL* engine =
      service ? service->GetDefaultSearchProvider() : nullptr;
  if (!engine) {
    return info;
  }
  info.id = EngineId(engine->GetEngineType(service->search_terms_data()));
  info.name = RowTitle(engine->short_name(), kMaxEngineName);
  return info;
}

void CluCommandHandler::SyncIndex(mojom::CommandSyncIndexInputPtr input,
                                  SyncIndexCallback callback) {
  auto answer = base::BindOnce(
      [](SyncIndexCallback callback, IndexDelta delta) {
        std::move(callback).Run(ValueResult(delta.ToValue()));
      },
      std::move(callback));
  CluHistoryIndex* index = CluHistoryIndex::GetForProfile(profile_);
  if (!window_ || !index) {
    // A private window keeps no history rows: an empty, full index.
    IndexDelta none;
    none.full = true;
    std::move(answer).Run(std::move(none));
    return;
  }
  // The guard checked it is an integer in [0, 2^53 - 1].
  index->Sync(static_cast<int64_t>(input->since_rev), std::move(answer));
}

void CluCommandHandler::Actions(mojom::CommandActionsInputPtr input,
                                ActionsCallback callback) {
  // Only the live session gets rows.
  if (!window_ || !window_->command_session() ||
      window_->command_session()->session_id != input->session_id) {
    std::move(callback).Run(ValueResult(base::Value(base::ListValue())));
    return;
  }
  std::move(callback).Run(ValueResult(base::Value(ActionRows())));
}

void CluCommandHandler::DeleteEntry(mojom::CommandDeleteEntryInputPtr input,
                                    DeleteEntryCallback callback) {
  // History only holds web pages; the exact stored URL is the key.
  const GURL url(input->url);
  if (!IsWebUrl(url) || url.IsAboutBlank()) {
    std::move(callback).Run(
        ErrorResult(mojom::ErrorCode::kBadPayload, "not a history URL"));
    return;
  }
  // The AllowDeletingBrowserHistory policy holds here as in every Chromium deletion path.
  if (!profile_->GetPrefs()->GetBoolean(prefs::kAllowDeletingBrowserHistory)) {
    std::move(callback).Run(ErrorResult(mojom::ErrorCode::kNotAllowed,
                                        "history deletion is off"));
    return;
  }
  if (CluHistoryIndex* index = CluHistoryIndex::GetForProfile(profile_)) {
    index->Delete(url);
  }
  std::move(callback).Run(NullResult());
}

void CluCommandHandler::CopyUrl(mojom::CommandCopyUrlInputPtr input,
                                CopyUrlCallback callback) {
  if (!window_) {
    std::move(callback).Run(WindowGone());
    return;
  }
  const bool markdown =
      input->format == mojom::CommandCopyUrlInputFormat::kMarkdown;
  const std::optional<std::string> copied =
      window_->CopyUrl(input->tab_id, markdown);
  std::move(callback).Run(ValueResult(base::Value(base::DictValue().Set(
      "copied", copied ? base::Value(*copied) : base::Value()))));
}

base::ListValue CluCommandHandler::ActionRows() {
  base::ListValue rows;
  if (!window_) {
    return rows;
  }
  for (const CommandSpec& spec : CommandTable()) {
    if (!spec.palette || spec.variant == CommandVariant::kMini ||
        !window_->CanRunCommand(spec.id)) {
      continue;
    }
    // Aliases and the chord come from the overlay's own copy of the table.
    rows.Append(base::DictValue().Set("id", spec.id).Set("title", spec.title));
  }
  AppendProviderRows(rows);
  return rows;
}

void CluCommandHandler::AppendProviderRows(base::ListValue& rows) {
  // A private window has one space and keeps the profile's engine as it is (PLAN 3.15).
  if (window_->is_private() || !window_->model()) {
    return;
  }
  const std::vector<SpaceEntry>& spaces =
      CluSpacesService::Get()->list().spaces();

  // "Switch to <space>" for every space but the one the window shows.
  for (const SpaceEntry& space : spaces) {
    if (space.id == window_->space_id()) {
      continue;
    }
    rows.Append(ProviderRow(
        kSpacesProvider, space.id,
        RowTitle(base::UTF8ToUTF16("Switch to " + space.name),
                 kMaxActionTitle),
        std::nullopt));
  }

  // "Move tab to <space>" for every space but the active row's.
  const std::string active = window_->ActiveRowId();
  const TabNode* row = active.empty() ? nullptr
                                      : window_->model()->tree().Get(active);
  if (row) {
    const std::string from =
        row->space_id.empty() ? window_->space_id() : row->space_id;
    for (const SpaceEntry& space : spaces) {
      if (space.id == from) {
        continue;
      }
      const std::u16string name =
          space.name.empty() ? u"Untitled"
                             : base::UTF8ToUTF16(space.name).substr(
                                   0, kMaxSpaceNameInRow);
      rows.Append(ProviderRow(
          kMoveTabProvider, space.id,
          RowTitle(u"Move tab to " + name, kMaxActionTitle), std::nullopt));
    }
  }

  // "Set search engine: <name>" for the profile's engines (Chromium's own, per profile).
  TemplateURLService* service =
      TemplateURLServiceFactory::GetForProfile(profile_);
  const TemplateURL* current =
      service ? service->GetDefaultSearchProvider() : nullptr;
  for (TemplateURL* engine : OfferedEngines(service)) {
    rows.Append(ProviderRow(
        kEngineProvider, base::NumberToString(engine->id().value()),
        RowTitle(u"Set search engine: " + engine->short_name(),
                 kMaxActionTitle),
        engine == current ? std::optional<std::string>("Current")
                          : std::nullopt));
  }
}

bool CluCommandHandler::RunProvided(std::string_view provider_id,
                                    std::string_view token) {
  if (!window_ || window_->is_private() || !window_->model() ||
      token.empty()) {
    return false;
  }
  const CluSpaceList& list = CluSpacesService::Get()->list();
  if (provider_id == kSpacesProvider) {
    const SpaceEntry* space = list.FindSpace(token);
    if (!space || space->id == window_->space_id()) {
      return false;
    }
    window_->CloseCommandBar();
    space_actions::SwitchWindow(window_.get(),
                                SwitchTarget{.space_id = space->id});
    return true;
  }
  if (provider_id == kMoveTabProvider) {
    const std::string active = window_->ActiveRowId();
    const TabNode* row = active.empty() ? nullptr
                                        : window_->model()->tree().Get(active);
    const SpaceEntry* space = list.FindSpace(token);
    if (!row || !space) {
      return false;
    }
    const std::string from =
        row->space_id.empty() ? window_->space_id() : row->space_id;
    if (space->id == from) {
      return false;
    }
    // Like a drop on the space's icon (02-tabs F8): Today rows to the top of its Today, pinned rows and favorites to
    // the end of its Pinned. Another profile asks first.
    DropTarget target;
    if (row->section == Section::kToday) {
      target = DropTarget::Today(space->id, 0);
    } else {
      target = DropTarget::Pinned(space->id, std::string(), 0);
      target.index =
          space_actions::IsCrossProfile(window_.get(), target)
              ? kEndOfList
              : static_cast<int>(
                    window_->model()->tree().Pinned(space->id).size());
    }
    window_->CloseCommandBar();
    space_actions::MoveRows(window_.get(), {active}, std::move(target),
                            /*confirmed=*/false, /*ask=*/true,
                            base::DoNothing());
    return true;
  }
  if (provider_id == kEngineProvider) {
    int64_t id = 0;
    if (!base::StringToInt64(token, &id)) {
      return false;
    }
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(profile_);
    for (TemplateURL* engine : OfferedEngines(service)) {
      if (engine->id() == TemplateURLID(id)) {
        window_->CloseCommandBar();
        if (engine == service->GetDefaultSearchProvider()) {
          return true;
        }
        service->SetUserSelectedDefaultSearchProvider(engine);
        window_->MarkDirty();
        return true;
      }
    }
    return false;
  }
  return false;
}

}  // namespace clu
