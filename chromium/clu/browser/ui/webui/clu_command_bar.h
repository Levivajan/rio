// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_COMMAND_BAR_H_
#define CLU_BROWSER_UI_WEBUI_CLU_COMMAND_BAR_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"
#include "components/omnibox/browser/autocomplete_controller.h"

class GURL;
class Profile;

namespace clu {

class CluWindowState;
struct CommandSessionSnapshot;

// The profile's default search engine as the bar shows it: `id` is the settings value the TS engine registry knows
// (src/shared/search/engines.ts; an engine it does not know reads as google), `name` the engine's own name.
struct SearchEngineInfo {
  std::string id = "google";
  std::string name;
};
SearchEngineInfo DefaultSearchEngine(Profile* profile);
// settings.set of search.engineId (M8): makes the profile's offered engine with that settings id its default. False
// when the profile offers none with that id (an engine Chromium does not prepopulate for the locale).
bool SetDefaultSearchEngine(Profile* profile, std::string_view engine_id);

// The text a suggestion row searches for: the engine's whole suggestion (a tail suggestion's contents hold only its
// tail). Null for a match that is no engine suggestion, or that another engine than the default (`default_keyword`)
// made, as keyword mode does for "bing.com harbor".
std::optional<std::u16string> SuggestionText(const AutocompleteMatch& match,
                                             std::u16string_view default_keyword);

// The command bar's handler (src/main/command/service.ts, docs/chromium/m5.md section 3). The overlay ranks tab and
// history rows itself; the history rows come from Chromium's HistoryService (command.syncIndex), suggestions from
// Chromium's AutocompleteController, actions from the key table and three providers, and every chosen
// row is checked again here before it runs.
class CluCommandHandler : public DefaultCommandHandler,
                          public AutocompleteController::Observer {
 public:
  // Provider ids of the dynamic action rows (PLAN decision 53).
  static constexpr char kSpacesProvider[] = "spaces";
  static constexpr char kMoveTabProvider[] = "tabs.moveTab";
  static constexpr char kEngineProvider[] = "search.engine";

  CluCommandHandler(base::WeakPtr<CluWindowState> window, Profile* profile);
  CluCommandHandler(const CluCommandHandler&) = delete;
  CluCommandHandler& operator=(const CluCommandHandler&) = delete;
  ~CluCommandHandler() override;

  // mojom::CommandHandler:
  void Open(mojom::CommandOpenInputPtr input, OpenCallback callback) override;
  void Close(mojom::CommandCloseInputPtr input,
             CloseCallback callback) override;
  void Execute(mojom::CommandExecuteInputPtr input,
               ExecuteCallback callback) override;
  void SyncIndex(mojom::CommandSyncIndexInputPtr input,
                 SyncIndexCallback callback) override;
  void Actions(mojom::CommandActionsInputPtr input,
               ActionsCallback callback) override;
  void Suggest(mojom::CommandSuggestInputPtr input,
               SuggestCallback callback) override;
  void DeleteEntry(mojom::CommandDeleteEntryInputPtr input,
                   DeleteEntryCallback callback) override;
  void CopyUrl(mojom::CommandCopyUrlInputPtr input,
               CopyUrlCallback callback) override;

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

  // Every action row the window offers now (ActionRow[] in src/shared/ipc/command.ts): the palette commands it can
  // run, in table order, then the providers' rows. The overlay matches them against the query with the shared
  // matcher (src/shared/command/actions.ts), so there is one matcher.
  base::ListValue ActionRows();

 private:
  // Answers the pending suggest request: the controller's search suggestions when `with_results`, else none.
  void FinishSuggest(bool with_results);

  // The search URL of the profile's default engine for `query`, or an empty GURL without one.
  GURL SearchUrl(const std::string& query);

  // Loads `url` where the session says: a new tab, or in place over the edit-url target.
  void OpenUrl(const CommandSessionSnapshot& session,
               const GURL& url,
               bool new_tab);

  // The provider rows, and running one. RunProvided() answers false for a token that is not one of the current
  // rows (the space or engine went away, the window shows another tab).
  void AppendProviderRows(base::ListValue& rows);
  bool RunProvided(std::string_view provider_id, std::string_view token);

  base::WeakPtr<CluWindowState> window_;
  const raw_ptr<Profile> profile_;

  std::unique_ptr<AutocompleteController> controller_;
  base::ScopedObservation<AutocompleteController,
                          AutocompleteController::Observer>
      controller_observation_{this};
  std::optional<double> pending_request_id_;
  std::string pending_session_id_;
  SuggestCallback pending_callback_;
  base::OneShotTimer suggest_timeout_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_COMMAND_BAR_H_
