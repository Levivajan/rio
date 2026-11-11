// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_TAB_ID_H_
#define CLU_BROWSER_UI_WEBUI_CLU_TAB_ID_H_

#include <optional>
#include <string>

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace clu {

// The UUID v4 a page carries in Clu state (TabState.id). With a model it is the id of the page's tab row, set before
// the page enters a tab strip; a page Clu did not open (a link with target=_blank, an extension) gets a fresh one when
// first asked, and its row takes that id. It lives on the WebContents, so it survives the tab moving between windows;
// a discard that swaps the WebContents copies it over (CluWindowState).
//
// The id also rides on every entry of the page's history (clu_row_stamp.h), so Chromium's session restore and
// Recently Closed bring a page back with it: a page first asked for its id takes the one its restored history
// carries, and WasRestored() is true.
class CluTabId : public content::WebContentsObserver,
                 public content::WebContentsUserData<CluTabId> {
 public:
  CluTabId(const CluTabId&) = delete;
  CluTabId& operator=(const CluTabId&) = delete;
  ~CluTabId() override;

  // The id of `contents`, created on first use.
  static const std::string& Get(content::WebContents* contents);
  // Gives `contents` the id of its row. Replaces an id it already has.
  static void Set(content::WebContents* contents, const std::string& id);
  static bool Has(content::WebContents* contents);
  // True when the id came from the page's restored history rather than from Clu.
  static bool WasRestored(content::WebContents* contents);
  // WasRestored(), once: the page's first adoption after the restore takes it, so a later move between windows is a
  // plain move.
  static bool TakeRestored(content::WebContents* contents);

 private:
  friend class content::WebContentsUserData<CluTabId>;
  CluTabId(content::WebContents* contents, std::string id, bool restored);

  // content::WebContentsObserver:
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;

  // Puts the id on every entry that lacks it, and hands those entries to the session service again: it may have
  // written a new entry before this observer stamped it.
  void StampHistory();

  const std::string id_;
  bool restored_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// A page that is no row: the blank tab a window keeps while it shows no row, so the tab strip never empties (an
// empty strip closes the window), and the new-tab page Chromium opens at startup. State leaves it out, and the
// layout hides the page area while it is the active tab.
class CluPlaceholder : public content::WebContentsUserData<CluPlaceholder> {
 public:
  CluPlaceholder(const CluPlaceholder&) = delete;
  CluPlaceholder& operator=(const CluPlaceholder&) = delete;
  ~CluPlaceholder() override;

  static void Mark(content::WebContents* contents);
  static bool Is(content::WebContents* contents);

 private:
  friend class content::WebContentsUserData<CluPlaceholder>;
  explicit CluPlaceholder(content::WebContents* contents);

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// The name the user gave a private window's page (tabs.rename). A private window keeps no rows, so the name lives on
// the page, goes with it to another private window, and ends with it.
class CluPageName : public content::WebContentsUserData<CluPageName> {
 public:
  CluPageName(const CluPageName&) = delete;
  CluPageName& operator=(const CluPageName&) = delete;
  ~CluPageName() override;

  // An empty name clears it, so the row shows the page title again.
  static void Set(content::WebContents* contents, const std::string& name);
  static std::optional<std::string> Get(content::WebContents* contents);

 private:
  friend class content::WebContentsUserData<CluPageName>;
  CluPageName(content::WebContents* contents, std::string name);

  std::string name_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// Chromium's new-tab page that never navigated: Clu has none, so a window that opens with it shows its restored row,
// the empty state or (a private window) the command bar instead.
bool IsNewTabPageContents(content::WebContents* contents);

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_TAB_ID_H_
