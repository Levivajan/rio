// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_UI_WEBUI_CLU_WINDOW_HANDLERS_H_
#define CLU_BROWSER_UI_WEBUI_CLU_WINDOW_HANDLERS_H_

#include "base/memory/weak_ptr.h"
#include "clu/browser/ui/webui/bridge/bridge_handlers.h"

// The window commands of M2 and M3 (docs/chromium/m2-shell.md, docs/chromium/m3.md). The guard in bridge_handlers.cc
// has already checked the surface and the payload caps; these check what the caps cannot (rows that exist, the URL
// policy, the move rules) and act on the window the WebUI belongs to. Every other method of these interfaces answers
// kNotImplemented.
namespace clu {

class CluWindowState;

class CluStateHandler : public DefaultStateHandler {
 public:
  explicit CluStateHandler(base::WeakPtr<CluWindowState> window);
  ~CluStateHandler() override;

  // mojom::StateHandler:
  void Get(GetCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluUiHandler : public DefaultUiHandler {
 public:
  explicit CluUiHandler(base::WeakPtr<CluWindowState> window);
  ~CluUiHandler() override;

  // mojom::UiHandler:
  void Ready(ReadyCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluOverlayHandler : public DefaultOverlayHandler {
 public:
  explicit CluOverlayHandler(base::WeakPtr<CluWindowState> window);
  ~CluOverlayHandler() override;

  // mojom::OverlayHandler:
  void Respond(mojom::OverlayRespondInputPtr input,
               RespondCallback callback) override;
  void Close(mojom::OverlayCloseInputPtr input,
             CloseCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluCommandsHandler : public DefaultCommandsHandler {
 public:
  explicit CluCommandsHandler(base::WeakPtr<CluWindowState> window);
  ~CluCommandsHandler() override;

  // mojom::CommandsHandler:
  void Run(mojom::CommandsRunInputPtr input, RunCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluTabsHandler : public DefaultTabsHandler {
 public:
  explicit CluTabsHandler(base::WeakPtr<CluWindowState> window);
  ~CluTabsHandler() override;

  // mojom::TabsHandler:
  void Create(mojom::TabsCreateInputPtr input,
              CreateCallback callback) override;
  void Activate(mojom::TabsActivateInputPtr input,
                ActivateCallback callback) override;
  void Close(mojom::TabsCloseInputPtr input, CloseCallback callback) override;
  void Duplicate(mojom::TabsDuplicateInputPtr input,
                 DuplicateCallback callback) override;
  void SetMuted(mojom::TabsSetMutedInputPtr input,
                SetMutedCallback callback) override;
  void Rename(mojom::TabsRenameInputPtr input,
              RenameCallback callback) override;
  void SetIcon(mojom::TabsSetIconInputPtr input,
               SetIconCallback callback) override;
  void Move(mojom::TabsMoveInputPtr input, MoveCallback callback) override;
  void Bulk(mojom::TabsBulkInputPtr input, BulkCallback callback) override;
  void TidyToday(mojom::TabsTidyTodayInputPtr input,
                 TidyTodayCallback callback) override;
  void TogglePin(mojom::TabsTogglePinInputPtr input,
                 TogglePinCallback callback) override;
  void ResetToPinned(mojom::TabsResetToPinnedInputPtr input,
                     ResetToPinnedCallback callback) override;
  void SetPinnedUrl(mojom::TabsSetPinnedUrlInputPtr input,
                    SetPinnedUrlCallback callback) override;
  void Delete(mojom::TabsDeleteInputPtr input,
              DeleteCallback callback) override;
  void ToNewWindow(mojom::TabsToNewWindowInputPtr input,
                   ToNewWindowCallback callback) override;
  void OpenMenu(mojom::TabsOpenMenuInputPtr input,
                OpenMenuCallback callback) override;
  void Hover(mojom::TabsHoverInputPtr input, HoverCallback callback) override;
  void OpenMail(mojom::TabsOpenMailInputPtr input,
                OpenMailCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluFoldersHandler : public DefaultFoldersHandler {
 public:
  explicit CluFoldersHandler(base::WeakPtr<CluWindowState> window);
  ~CluFoldersHandler() override;

  // mojom::FoldersHandler:
  void Create(mojom::FoldersCreateInputPtr input,
              CreateCallback callback) override;
  void CreateLive(mojom::FoldersCreateLiveInputPtr input,
                  CreateLiveCallback callback) override;
  void SetCollapsed(mojom::FoldersSetCollapsedInputPtr input,
                    SetCollapsedCallback callback) override;
  void Delete(mojom::FoldersDeleteInputPtr input,
              DeleteCallback callback) override;
  void Peek(mojom::FoldersPeekInputPtr input, PeekCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluSectionsHandler : public DefaultSectionsHandler {
 public:
  explicit CluSectionsHandler(base::WeakPtr<CluWindowState> window);
  ~CluSectionsHandler() override;

  // mojom::SectionsHandler:
  void SetPinnedCollapsed(mojom::SectionsSetPinnedCollapsedInputPtr input,
                          SetPinnedCollapsedCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluArchiveHandler : public DefaultArchiveHandler {
 public:
  explicit CluArchiveHandler(base::WeakPtr<CluWindowState> window);
  ~CluArchiveHandler() override;

  // mojom::ArchiveHandler:
  void List(mojom::ArchiveListInputPtr input, ListCallback callback) override;
  void Restore(mojom::ArchiveRestoreInputPtr input,
               RestoreCallback callback) override;
  void Delete(mojom::ArchiveDeleteInputPtr input,
              DeleteCallback callback) override;
  void Open(OpenCallback callback) override;
  void Close(CloseCallback callback) override;
  void SetWidth(mojom::ArchiveSetWidthInputPtr input,
                SetWidthCallback callback) override;
  void ClearToday(mojom::ArchiveClearTodayInputPtr input,
                  ClearTodayCallback callback) override;
  void UndoClear(UndoClearCallback callback) override;
  void ReopenClosed(ReopenClosedCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluSwitcherHandler : public DefaultSwitcherHandler {
 public:
  explicit CluSwitcherHandler(base::WeakPtr<CluWindowState> window);
  ~CluSwitcherHandler() override;

  // mojom::SwitcherHandler:
  void Respond(mojom::SwitcherRespondInputPtr input,
               RespondCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// settings.* (docs/plan/07-data.md, docs/chromium/m8.md): every key of the settings registry. Global keys go to
// Local State (CluAppSettings), tabs.archiveAfterHours to the scope profile's model, search.engineId to that
// profile's default engine. settings.open shows the app settings window at a tab (docs/chromium/parity-settings.md);
// settings.openBrowser opens Chromium's own settings (rio://settings) at a page as a row of the window's space.
class CluSettingsHandler : public DefaultSettingsHandler {
 public:
  explicit CluSettingsHandler(base::WeakPtr<CluWindowState> window);
  ~CluSettingsHandler() override;

  // mojom::SettingsHandler:
  void Open(mojom::SettingsOpenInputPtr input, OpenCallback callback) override;
  void OpenBrowser(mojom::SettingsOpenBrowserInputPtr input,
                   OpenBrowserCallback callback) override;
  void Set(mojom::SettingsSetInputPtr input, SetCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
  base::WeakPtrFactory<CluSettingsHandler> weak_ptr_factory_{this};
};

class CluNavHandler : public DefaultNavHandler {
 public:
  explicit CluNavHandler(base::WeakPtr<CluWindowState> window);
  ~CluNavHandler() override;

  // mojom::NavHandler:
  void Back(mojom::NavBackInputPtr input, BackCallback callback) override;
  void Forward(mojom::NavForwardInputPtr input,
               ForwardCallback callback) override;
  void Stop(mojom::NavStopInputPtr input, StopCallback callback) override;
  void Reload(mojom::NavReloadInputPtr input,
              ReloadCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// zoom.step: Chromium's page zoom steps on the tab the payload names in the calling window.
class CluZoomHandler : public DefaultZoomHandler {
 public:
  explicit CluZoomHandler(base::WeakPtr<CluWindowState> window);
  ~CluZoomHandler() override;

  // mojom::ZoomHandler:
  void Step(mojom::ZoomStepInputPtr input, StepCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluShellHandler : public DefaultShellHandler {
 public:
  explicit CluShellHandler(base::WeakPtr<CluWindowState> window);
  ~CluShellHandler() override;

  // mojom::ShellHandler:
  void ToggleSidebar(ToggleSidebarCallback callback) override;
  void ToggleFocusMode(ToggleFocusModeCallback callback) override;
  void ResetSidebarWidth(ResetSidebarWidthCallback callback) override;
  void SetSidebarWidth(mojom::ShellSetSidebarWidthInputPtr input,
                       SetSidebarWidthCallback callback) override;
  // M8: the long-press and right-click history menu of back and forward (12.3 fourth box), Chromium's own
  // BackForwardMenuModel as a native menu under the button.
  void OpenNavHistory(mojom::ShellOpenNavHistoryInputPtr input,
                      OpenNavHistoryCallback callback) override;
  // The URL pill's right-click menu, Show as Toolbar or Hide Toolbar, as a native menu at the pointer.
  void OpenToolbarMenu(mojom::ShellOpenToolbarMenuInputPtr input,
                       OpenToolbarMenuCallback callback) override;
  // The hover peek (12.3 third box): the frame's hot zone dwell and the overlay's leave grace ask to open or close it,
  // the overlay's peek reports the pointer. The browser runs the same rules on its own clock
  // (CluWindowSidebarPeek), so these only ever ask.
  void SetSidebarPeek(mojom::ShellSetSidebarPeekInputPtr input,
                      SetSidebarPeekCallback callback) override;
  void PointerAlive(PointerAliveCallback callback) override;
  void HoldSidebarPeek(mojom::ShellHoldSidebarPeekInputPtr input,
                       HoldSidebarPeekCallback callback) override;
  // The frame's tooltips draw in the overlay so the page card never covers them.
  void Tooltip(mojom::ShellTooltipInputPtr input,
               TooltipCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// spaces.* (docs/plan/03-spaces.md, docs/chromium/m4.md): the registry checks names, icons, colors and caps; the
// window comes from the calling WebUI. A private window has no spaces: every command but the swipe answers
// not-allowed there, and the swipe does nothing.
class CluSpacesHandler : public DefaultSpacesHandler {
 public:
  explicit CluSpacesHandler(base::WeakPtr<CluWindowState> window);
  ~CluSpacesHandler() override;

  // mojom::SpacesHandler:
  void Create(mojom::SpacesCreateInputPtr input,
              CreateCallback callback) override;
  void Update(mojom::SpacesUpdateInputPtr input,
              UpdateCallback callback) override;
  void Reorder(mojom::SpacesReorderInputPtr input,
               ReorderCallback callback) override;
  void Delete(mojom::SpacesDeleteInputPtr input,
              DeleteCallback callback) override;
  void Switch(mojom::SpacesSwitchInputPtr input,
              SwitchCallback callback) override;
  void SetProfile(mojom::SpacesSetProfileInputPtr input,
                  SetProfileCallback callback) override;
  void OpenOverflow(mojom::SpacesOpenOverflowInputPtr input,
                    OpenOverflowCallback callback) override;
  void OpenEditor(mojom::SpacesOpenEditorInputPtr input,
                  OpenEditorCallback callback) override;
  void OpenIconMenu(mojom::SpacesOpenIconMenuInputPtr input,
                    OpenIconMenuCallback callback) override;
  void Swipe(mojom::SpacesSwipeInputPtr input,
             SwipeCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// profiles.*: each Clu profile is a Chromium profile (CluSpacesService).
class CluProfilesHandler : public DefaultProfilesHandler {
 public:
  explicit CluProfilesHandler(base::WeakPtr<CluWindowState> window);
  ~CluProfilesHandler() override;

  // mojom::ProfilesHandler:
  void Create(mojom::ProfilesCreateInputPtr input,
              CreateCallback callback) override;
  void Rename(mojom::ProfilesRenameInputPtr input,
              RenameCallback callback) override;
  void Delete(mojom::ProfilesDeleteInputPtr input,
              DeleteCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// window.newPrivate: Chromium's incognito window with Clu's private shell (PLAN 3.15).
class CluWindowHandler : public DefaultWindowHandler {
 public:
  explicit CluWindowHandler(base::WeakPtr<CluWindowState> window);
  ~CluWindowHandler() override;

  // mojom::WindowHandler:
  void NewPrivate(NewPrivateCallback callback) override;
  // window.close: the Mini window's Close button (M6).
  void Close(CloseCallback callback) override;
  // window.minimize and window.toggleFullscreen: the floating sidebar's window buttons, where the native ones
  // sit under its overlay.
  void Minimize(MinimizeCallback callback) override;
  void ToggleFullscreen(ToggleFullscreenCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

// M6 (docs/chromium/m6.md): split view over Chromium's two-pane split tabs, the
// sidebar media card, the Peek and the Mini window. The guard has checked the
// caps; these check the cross-field rules zod refines (unique ids, no drop on
// itself, sizes summing to 1) and what the window has.
class CluSplitHandler : public DefaultSplitHandler {
 public:
  explicit CluSplitHandler(base::WeakPtr<CluWindowState> window);
  ~CluSplitHandler() override;

  // mojom::SplitHandler:
  void Create(mojom::SplitCreateInputPtr input,
              CreateCallback callback) override;
  void DropTab(mojom::SplitDropTabInputPtr input,
               DropTabCallback callback) override;
  void Swap(mojom::SplitSwapInputPtr input, SwapCallback callback) override;
  void Resize(mojom::SplitResizeInputPtr input,
              ResizeCallback callback) override;
  void Equalize(mojom::SplitEqualizeInputPtr input,
                EqualizeCallback callback) override;
  void RemovePane(mojom::SplitRemovePaneInputPtr input,
                  RemovePaneCallback callback) override;
  void FocusPane(mojom::SplitFocusPaneInputPtr input,
                 FocusPaneCallback callback) override;
  void DragState(mojom::SplitDragStateInputPtr input,
                 DragStateCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluMediaHandler : public DefaultMediaHandler {
 public:
  explicit CluMediaHandler(base::WeakPtr<CluWindowState> window);
  ~CluMediaHandler() override;

  // mojom::MediaHandler:
  void Toggle(mojom::MediaToggleInputPtr input,
              ToggleCallback callback) override;
  void Previous(mojom::MediaPreviousInputPtr input,
                PreviousCallback callback) override;
  void Next(mojom::MediaNextInputPtr input, NextCallback callback) override;
  void Focus(mojom::MediaFocusInputPtr input, FocusCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluPeekHandler : public DefaultPeekHandler {
 public:
  explicit CluPeekHandler(base::WeakPtr<CluWindowState> window);
  ~CluPeekHandler() override;

  // mojom::PeekHandler:
  void Close(CloseCallback callback) override;
  void Expand(ExpandCallback callback) override;
  void ToSplit(ToSplitCallback callback) override;
  void Nav(mojom::PeekNavInputPtr input, NavCallback callback) override;
  void AnimationDone(mojom::PeekAnimationDoneInputPtr input,
                     AnimationDoneCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

class CluMiniHandler : public DefaultMiniHandler {
 public:
  explicit CluMiniHandler(base::WeakPtr<CluWindowState> window);
  ~CluMiniHandler() override;

  // mojom::MiniHandler:
  void Open(mojom::MiniOpenInputPtr input, OpenCallback callback) override;
  void MoveToSpace(mojom::MiniMoveToSpaceInputPtr input,
                   MoveToSpaceCallback callback) override;
  void PickSpace(PickSpaceCallback callback) override;
  void CancelMove(CancelMoveCallback callback) override;
  void Back(BackCallback callback) override;

 private:
  base::WeakPtr<CluWindowState> window_;
};

}  // namespace clu

#endif  // CLU_BROWSER_UI_WEBUI_CLU_WINDOW_HANDLERS_H_
