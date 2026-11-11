// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_EXTENSION_ACTIONS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_EXTENSION_ACTIONS_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "clu/browser/ui/webui/clu_site_state.h"

class BrowserWindowInterface;
class Profile;

namespace content {
class WebContents;
}  // namespace content

// The action row and the extensions menu over Chromium's own toolbar actions (docs/chromium/m7.md): the profile's
// ToolbarActionsModel lists the actions and keeps the pins (Chromium's pinned_extensions pref), the window's hidden
// extensions container runs them, so popups, side panels, action clicks and permission requests behave exactly as in
// Chrome. Clu's component extension is left out.
namespace clu {

// Every action of the profile for the page `active` shows, pinned first in pin order, then by name; nullopt when the
// profile has no extension (the row hides).
std::optional<ActionRowSnapshot> BuildActionRow(BrowserWindowInterface* browser,
                                                Profile* profile,
                                                const std::string& profile_id,
                                                content::WebContents* active);

// profiles[id].extensions: every extension chrome://extensions would list, enabled or not.
std::vector<ExtensionInfoSnapshot> BuildExtensionInfos(Profile* profile);

// Runs the action of extension `id` on the window's page, as a click on its toolbar button would: its popup, its side
// panel or its onClicked event. False for an unknown or Clu's own extension.
bool RunExtensionAction(BrowserWindowInterface* browser, const std::string& id);

// Pins or unpins the action in the row. False for an unknown extension, one policy keeps pinned, or no change.
bool SetExtensionPinned(Profile* profile, const std::string& id, bool pinned);

// Runs `callback` when the icon of extension `id`'s action changes (it loads, setIcon). An empty subscription when the
// window has no such action.
base::CallbackListSubscription ObserveActionIcon(BrowserWindowInterface* browser,
                                                 const std::string& id,
                                                 base::RepeatingClosure callback);

// True for an extension of the profile the action row lists.
bool IsListedExtension(Profile* profile, const std::string& id);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_EXTENSION_ACTIONS_H_
