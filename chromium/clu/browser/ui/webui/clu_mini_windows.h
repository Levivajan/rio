// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_MINI_WINDOWS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_MINI_WINDOWS_H_

#include <memory>
#include <string>
#include <vector>

class BrowserWindowInterface;
class GURL;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace clu {

class CluWindowState;

// Mini windows across the app (docs/plan/05-multitask.md "Mini window",
// decisions 27 and 54, docs/chromium/m6.md): a Chromium popup window Clu makes
// with the Clu layout in Mini mode, holding one page, its toolbar the frame
// WebUI's MiniApp. Each window's own state (target space, confirm strip, idle
// close, archive on close) lives in its CluWindowMultitask.
namespace mini_windows {

// Opens `url` (http, https or about:blank; anything else is dropped) in a new
// Mini window of `profile`, 880 by 620 centered on the display under the cursor
// and cascaded 22 DIP per open one. With 6 open, the URL opens as a Today tab
// at the top of the last focused main window's space with the cap toast.
// `command_bar` opens the command bar in mini mode (Cmd+Option+N). Returns the
// page that shows the URL (the Mini window's, or the Today tab's at the cap),
// or null when nothing opened.
content::WebContents* Open(Profile* profile, const GURL& url, bool command_bar);

// The open Mini windows, in creation order.
std::vector<CluWindowState*> All();

// The most recently focused main window (normal, not private, not hidden by a
// profile switch), or null.
CluWindowState* LastMainWindow();

// A Mini window's page moves into `space_id` of the same profile as the top
// Today tab, without a reload: the window showing that space, else the last
// main window switched to it, else a new main window on it. The window comes to
// the front.
void AdoptIntoSpace(Profile* profile,
                    const std::string& space_id,
                    std::unique_ptr<content::WebContents> contents);

// The confirmed move into a space of another profile: `url` opens fresh there.
void OpenUrlInSpace(const std::string& space_id, const GURL& url);

// mini.pickSpace: the native menu of spaces at the pointer; a pick moves the
// page there.
void ShowSpacePicker(CluWindowState& mini);

// Tests: Mini windows show without taking focus (05 "In test mode it uses
// showInactive()").
void SetShowInactiveForTesting(bool inactive);

}  // namespace mini_windows

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_MINI_WINDOWS_H_
