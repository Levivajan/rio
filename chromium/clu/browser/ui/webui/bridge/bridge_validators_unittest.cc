// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/ui/webui/bridge/bridge_validators.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "clu/common/mojom/archive.mojom.h"
#include "clu/common/mojom/capabilities.mojom.h"
#include "clu/common/mojom/command.mojom.h"
#include "clu/common/mojom/core.mojom.h"
#include "clu/common/mojom/data.mojom.h"
#include "clu/common/mojom/engine.mojom.h"
#include "clu/common/mojom/multitask.mojom.h"
#include "clu/common/mojom/shell.mojom.h"
#include "clu/common/mojom/spaces.mojom.h"
#include "clu/common/mojom/split.mojom.h"
#include "clu/common/mojom/tabs.mojom.h"
#include "clu/common/mojom/theme.mojom.h"
#include "clu/common/mojom/types.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

// Junk for every command M2 to M6 implement must fail the caps, so the guard
// answers kBadPayload before a handler runs (docs/chromium/PLAN.md 1.3
// "Bridge": a gtest per command). The valid case passes, so a cap that rejects
// everything shows up too.
namespace clu {
namespace {

constexpr char kId[] = "00000000-0000-4000-8000-000500000015";
constexpr char kNotAnId[] = "tab-1";
constexpr char kBadUtf8[] = "\xC3";
std::string TooLongUrl() {
  return std::string(32769, 'a');
}

TEST(CluBridgeValidatorsTest, TabsCreate) {
  EXPECT_TRUE(IsValid(*mojom::TabsCreateInput::New(std::nullopt,
                                                   "https://a.test/", true)));
  EXPECT_TRUE(IsValid(*mojom::TabsCreateInput::New(kId, "", std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::TabsCreateInput::New(kNotAnId, "https://a.test/",
                                                    std::nullopt)));
  EXPECT_FALSE(
      IsValid(*mojom::TabsCreateInput::New(std::nullopt, TooLongUrl(), true)));
  EXPECT_FALSE(
      IsValid(*mojom::TabsCreateInput::New(std::nullopt, kBadUtf8, true)));
}

TEST(CluBridgeValidatorsTest, TabsActivateAndClose) {
  EXPECT_TRUE(IsValid(*mojom::TabsActivateInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::TabsActivateInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TabsActivateInput::New("")));
  EXPECT_TRUE(IsValid(*mojom::TabsCloseInput::New(std::nullopt)));
  EXPECT_TRUE(IsValid(*mojom::TabsCloseInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::TabsCloseInput::New(kNotAnId)));
}

TEST(CluBridgeValidatorsTest, Navigation) {
  EXPECT_TRUE(IsValid(*mojom::NavBackInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::NavBackInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::NavForwardInput::New(kBadUtf8)));
  EXPECT_FALSE(IsValid(*mojom::NavStopInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::NavReloadInput::New(kId, true)));
  EXPECT_FALSE(IsValid(*mojom::NavReloadInput::New(kNotAnId, std::nullopt)));
}

TEST(CluBridgeValidatorsTest, ShellSidebarWidth) {
  EXPECT_TRUE(IsValid(*mojom::ShellSetSidebarWidthInput::New(300, true)));
  EXPECT_FALSE(IsValid(*mojom::ShellSetSidebarWidthInput::New(
      std::numeric_limits<double>::quiet_NaN(), std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::ShellSetSidebarWidthInput::New(
      std::numeric_limits<double>::infinity(), std::nullopt)));
}

TEST(CluBridgeValidatorsTest, CommandsRunAndOverlayClose) {
  EXPECT_TRUE(IsValid(*mojom::CommandsRunInput::New("sidebar.toggle")));
  EXPECT_FALSE(IsValid(*mojom::CommandsRunInput::New("sidebar")));
  EXPECT_FALSE(IsValid(*mojom::CommandsRunInput::New("1sidebar.toggle")));
  EXPECT_FALSE(IsValid(*mojom::CommandsRunInput::New(std::string(65, 'a') + ".b")));
  EXPECT_TRUE(IsValid(*mojom::OverlayCloseInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::OverlayCloseInput::New(kNotAnId)));
}

TEST(CluBridgeValidatorsTest, CommandBarSession) {
  EXPECT_TRUE(IsValid(*mojom::CommandCloseInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::CommandCloseInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::CommandSuggestInput::New(kId, 3, "query")));
  EXPECT_FALSE(IsValid(*mojom::CommandSuggestInput::New(kId, 1.5, "query")));
  EXPECT_FALSE(IsValid(*mojom::CommandSuggestInput::New(kId, -1, "query")));
  EXPECT_FALSE(IsValid(
      *mojom::CommandSuggestInput::New(kId, 3, std::string(257, 'q'))));
  EXPECT_FALSE(IsValid(*mojom::CommandSuggestInput::New(kNotAnId, 3, "q")));
}

TEST(CluBridgeValidatorsTest, CommandExecuteChecksTheChosenRow) {
  const auto execute = [](mojom::CommandResultPtr result,
                          std::string session = kId) {
    return mojom::CommandExecuteInput::New(
        session, std::move(result),
        mojom::CommandExecuteInputDisposition::kDefault, "typed");
  };
  EXPECT_TRUE(IsValid(*execute(mojom::CommandResult::NewNavigate(
      mojom::CommandResultNavigate::New("navigate", "https://a.test/")))));
  // The literal discriminator must match its variant.
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewNavigate(
      mojom::CommandResultNavigate::New("search", "https://a.test/")))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewNavigate(
      mojom::CommandResultNavigate::New("navigate", TooLongUrl())))));
  EXPECT_FALSE(IsValid(*execute(
      mojom::CommandResult::NewSearch(mojom::CommandResultSearch::New(
          "search", std::string(33, 'e'), "query")))));
  EXPECT_FALSE(IsValid(*execute(
      mojom::CommandResult::NewTab(mojom::CommandResultTab::New(
          "tab", kNotAnId, kId, false, "https://a.test/", "Title")))));
  EXPECT_FALSE(IsValid(*execute(
      mojom::CommandResult::NewTab(mojom::CommandResultTab::New(
          "tab", kId, kId, false, "https://a.test/", std::string(1025, 't'))))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewReload(
      mojom::CommandResultReload::New("reload", kNotAnId)))));
  EXPECT_FALSE(IsValid(*execute(
      mojom::CommandResult::NewNavigate(
          mojom::CommandResultNavigate::New("navigate", "https://a.test/")),
      kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::CommandExecuteInput::New(
      kId,
      mojom::CommandResult::NewNavigate(
          mojom::CommandResultNavigate::New("navigate", "https://a.test/")),
      mojom::CommandExecuteInputDisposition::kNewTab,
      std::string(8193, 'i'))));
}

// M5: the rest of the command bar (docs/chromium/m5.md): the history sync, actions, deleting a history row, Copy URL
// and the palette and provider rows command.execute runs.
TEST(CluBridgeValidatorsTest, CommandBarHistoryActionsAndCopy) {
  EXPECT_TRUE(IsValid(*mojom::CommandSyncIndexInput::New(0)));
  EXPECT_TRUE(IsValid(*mojom::CommandSyncIndexInput::New(9007199254740991.0)));
  EXPECT_FALSE(IsValid(*mojom::CommandSyncIndexInput::New(-1)));
  EXPECT_FALSE(IsValid(*mojom::CommandSyncIndexInput::New(2.5)));
  EXPECT_FALSE(IsValid(*mojom::CommandSyncIndexInput::New(9007199254740992.0)));
  EXPECT_FALSE(IsValid(*mojom::CommandSyncIndexInput::New(
      std::numeric_limits<double>::quiet_NaN())));
  EXPECT_FALSE(IsValid(*mojom::CommandSyncIndexInput::New(
      std::numeric_limits<double>::infinity())));

  EXPECT_TRUE(IsValid(*mojom::CommandActionsInput::New(kId, "")));
  EXPECT_TRUE(IsValid(*mojom::CommandActionsInput::New(kId, "toggle")));
  EXPECT_FALSE(IsValid(*mojom::CommandActionsInput::New(kNotAnId, "toggle")));
  EXPECT_FALSE(
      IsValid(*mojom::CommandActionsInput::New(kId, std::string(257, 'q'))));
  EXPECT_FALSE(IsValid(*mojom::CommandActionsInput::New(kId, kBadUtf8)));

  EXPECT_TRUE(
      IsValid(*mojom::CommandDeleteEntryInput::New("https://a.test/page")));
  EXPECT_FALSE(IsValid(*mojom::CommandDeleteEntryInput::New(TooLongUrl())));
  EXPECT_FALSE(IsValid(*mojom::CommandDeleteEntryInput::New(kBadUtf8)));

  EXPECT_TRUE(IsValid(
      *mojom::CommandCopyUrlInput::New(std::nullopt, std::nullopt)));
  EXPECT_TRUE(IsValid(*mojom::CommandCopyUrlInput::New(
      kId, mojom::CommandCopyUrlInputFormat::kMarkdown)));
  EXPECT_FALSE(IsValid(
      *mojom::CommandCopyUrlInput::New(std::string(kNotAnId), std::nullopt)));
}

TEST(CluBridgeValidatorsTest, CommandExecuteChecksPaletteAndProviderRows) {
  const auto execute = [](mojom::CommandResultPtr result) {
    return mojom::CommandExecuteInput::New(
        kId, std::move(result),
        mojom::CommandExecuteInputDisposition::kDefault, "");
  };
  EXPECT_TRUE(IsValid(*execute(mojom::CommandResult::NewCommand(
      mojom::CommandResultCommand::New("command", "sidebar.toggle")))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewCommand(
      mojom::CommandResultCommand::New("command", "sidebar")))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewCommand(
      mojom::CommandResultCommand::New("command", "sidebar.toggle()")))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewCommand(
      mojom::CommandResultCommand::New("navigate", "sidebar.toggle")))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewCommand(
      mojom::CommandResultCommand::New("command",
                                       std::string(65, 'a') + ".b")))));
  EXPECT_TRUE(IsValid(*execute(mojom::CommandResult::NewProvided(
      mojom::CommandResultProvided::New("provided", "spaces", kId)))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewProvided(
      mojom::CommandResultProvided::New("provided", std::string(65, 'p'),
                                        kId)))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewProvided(
      mojom::CommandResultProvided::New("provided", "spaces",
                                        std::string(257, 't'))))));
  EXPECT_FALSE(IsValid(*execute(mojom::CommandResult::NewProvided(
      mojom::CommandResultProvided::New("provided", "spaces", kBadUtf8)))));
}

// M3: the tab, folder, section, archive, switcher and settings commands (docs/chromium/m3.md).

TEST(CluBridgeValidatorsTest, TabsRowCommands) {
  EXPECT_TRUE(IsValid(*mojom::TabsDuplicateInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::TabsDuplicateInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::TabsSetMutedInput::New(kId, true)));
  EXPECT_FALSE(IsValid(*mojom::TabsSetMutedInput::New(kNotAnId, true)));
  EXPECT_TRUE(IsValid(*mojom::TabsRenameInput::New(kId, std::string(200, 'n'))));
  EXPECT_FALSE(
      IsValid(*mojom::TabsRenameInput::New(kId, std::string(201, 'n'))));
  EXPECT_FALSE(IsValid(*mojom::TabsRenameInput::New(kId, kBadUtf8)));
  EXPECT_TRUE(IsValid(*mojom::TabsSetIconInput::New(kId, std::nullopt)));
  EXPECT_FALSE(
      IsValid(*mojom::TabsSetIconInput::New(kId, std::string(33, 'i'))));
  EXPECT_TRUE(IsValid(*mojom::TabsTogglePinInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::TabsTogglePinInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TabsResetToPinnedInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::TabsSetPinnedUrlInput::New(kId, "https://a.test/")));
  EXPECT_FALSE(IsValid(*mojom::TabsSetPinnedUrlInput::New(kId, TooLongUrl())));
  EXPECT_FALSE(IsValid(*mojom::TabsDeleteInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::TabsToNewWindowInput::New(kNotAnId)));
}

TEST(CluBridgeValidatorsTest, TabsMoveChecksTheDropTarget) {
  const auto move = [](mojom::DropTargetPtr target,
                       std::string node = kId) {
    return mojom::TabsMoveInput::New(node, std::move(target), std::nullopt,
                                     true, std::nullopt);
  };
  EXPECT_TRUE(IsValid(*move(mojom::DropTarget::NewFavorite(
      mojom::DropTargetFavorite::New("favorite", 0)))));
  EXPECT_TRUE(IsValid(*move(mojom::DropTarget::NewPinned(
      mojom::DropTargetPinned::New("pinned", kId, std::nullopt, 3)))));
  EXPECT_TRUE(IsValid(*move(
      mojom::DropTarget::NewToday(mojom::DropTargetToday::New("today", kId, 0)))));
  EXPECT_FALSE(IsValid(*move(mojom::DropTarget::NewFavorite(
      mojom::DropTargetFavorite::New("today", 0)))));
  EXPECT_FALSE(IsValid(*move(mojom::DropTarget::NewFavorite(
      mojom::DropTargetFavorite::New("favorite", 10001)))));
  EXPECT_FALSE(IsValid(*move(mojom::DropTarget::NewFavorite(
      mojom::DropTargetFavorite::New("favorite", -1)))));
  EXPECT_FALSE(IsValid(*move(mojom::DropTarget::NewPinned(
      mojom::DropTargetPinned::New("pinned", kId, kNotAnId, 0)))));
  EXPECT_FALSE(IsValid(*move(mojom::DropTarget::NewToday(
      mojom::DropTargetToday::New("today", kNotAnId, 0)))));
  EXPECT_FALSE(IsValid(*move(mojom::DropTarget::NewFavorite(
                                 mojom::DropTargetFavorite::New("favorite", 0)),
                             kNotAnId)));
}

TEST(CluBridgeValidatorsTest, TabsBulkTakesOneToTwoHundredIds) {
  const auto action = [](std::vector<std::string> ids) {
    return mojom::TabsBulkInput::NewAction(mojom::TabsBulkInputAction::New(
        std::move(ids), mojom::TabsBulkInputActionAction::kArchive));
  };
  EXPECT_TRUE(IsValid(*action({kId})));
  EXPECT_TRUE(IsValid(*action(std::vector<std::string>(200, kId))));
  EXPECT_FALSE(IsValid(*action({})));
  EXPECT_FALSE(IsValid(*action(std::vector<std::string>(201, kId))));
  EXPECT_FALSE(IsValid(*action({kId, kNotAnId})));
  const auto move = [](std::string literal) {
    return mojom::TabsBulkInput::NewMove(mojom::TabsBulkInputMove::New(
        std::vector<std::string>{kId}, std::move(literal),
        mojom::DropTarget::NewToday(mojom::DropTargetToday::New("today", kId, 0)),
        std::nullopt, true, std::nullopt));
  };
  EXPECT_TRUE(IsValid(*move("move")));
  EXPECT_FALSE(IsValid(*move("archive")));
}

TEST(CluBridgeValidatorsTest, TabsOpenMenuAndHover) {
  const auto menu = [](std::optional<std::vector<std::string>> ids,
                       mojom::AnchorPtr anchor) {
    return mojom::TabsOpenMenuInput::New(mojom::TabsOpenMenuInputMenu::kRow,
                                         kId, std::move(ids), std::move(anchor),
                                         std::nullopt, std::nullopt);
  };
  EXPECT_TRUE(IsValid(*menu(std::nullopt, mojom::Anchor::NewPoint(
                                              mojom::Point::New(10, 20)))));
  EXPECT_TRUE(IsValid(*menu(std::vector<std::string>(200, kId),
                            mojom::Anchor::NewRect(
                                mojom::Rect::New(0, 0, 100, 30)))));
  EXPECT_FALSE(IsValid(*menu(std::vector<std::string>(201, kId),
                             mojom::Anchor::NewPoint(mojom::Point::New(0, 0)))));
  EXPECT_FALSE(IsValid(*menu(
      std::nullopt, mojom::Anchor::NewPoint(mojom::Point::New(
                        std::numeric_limits<double>::quiet_NaN(), 0)))));
  EXPECT_TRUE(IsValid(*mojom::TabsHoverInput::New(std::nullopt, nullptr, std::nullopt)));
  EXPECT_TRUE(IsValid(
      *mojom::TabsHoverInput::New(kId, mojom::Rect::New(8, 100, 240, 30), true)));
  EXPECT_FALSE(IsValid(*mojom::TabsHoverInput::New(
      kId, mojom::Rect::New(std::numeric_limits<double>::infinity(), 0, 1, 1),
      std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::TabsHoverInput::New(kNotAnId, nullptr, std::nullopt)));
}

TEST(CluBridgeValidatorsTest, FoldersAndSections) {
  EXPECT_TRUE(IsValid(*mojom::FoldersCreateInput::New(kId, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::FoldersCreateInput::New(kNotAnId, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::FoldersCreateInput::New(kId, kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::FoldersSetCollapsedInput::New(kNotAnId, true)));
  EXPECT_FALSE(IsValid(*mojom::FoldersDeleteInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::SectionsSetPinnedCollapsedInput::New(kId, true)));
  EXPECT_FALSE(
      IsValid(*mojom::SectionsSetPinnedCollapsedInput::New(kNotAnId, true)));
}

TEST(CluBridgeValidatorsTest, ArchiveAndSwitcher) {
  EXPECT_TRUE(IsValid(*mojom::ArchiveListInput::New(kId, "50%", std::nullopt,
                                                    100)));
  EXPECT_FALSE(
      IsValid(*mojom::ArchiveListInput::New(kId, std::nullopt, std::nullopt, 0)));
  EXPECT_FALSE(IsValid(
      *mojom::ArchiveListInput::New(kId, std::nullopt, std::nullopt, 101)));
  EXPECT_FALSE(IsValid(*mojom::ArchiveListInput::New(
      kId, std::string(201, 'q'), std::nullopt, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::ArchiveListInput::New(
      kId, std::nullopt, std::string(257, 'c'), std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::ArchiveListInput::New(
      kNotAnId, std::nullopt, std::nullopt, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::ArchiveRestoreInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::ArchiveDeleteInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::ArchiveClearTodayInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::ArchiveClearTodayInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::SwitcherRespondInput::New(
      mojom::SwitcherRespondInputAction::kCommit, kId)));
  EXPECT_FALSE(IsValid(*mojom::SwitcherRespondInput::New(
      mojom::SwitcherRespondInputAction::kCancel, kNotAnId)));
}

TEST(CluBridgeValidatorsTest, SettingsSet) {
  EXPECT_TRUE(
      IsValid(*mojom::SettingsSetInput::New("tabs.archiveAfterHours", kId, "24")));
  EXPECT_TRUE(IsValid(
      *mojom::SettingsSetInput::New("appearance.mode", "global", "\"dark\"")));
  EXPECT_FALSE(IsValid(
      *mojom::SettingsSetInput::New("tabs.archiveAfterHours", "profile", "24")));
  EXPECT_FALSE(IsValid(
      *mojom::SettingsSetInput::New(std::string(65, 'k'), kId, "24")));
  EXPECT_FALSE(
      IsValid(*mojom::SettingsSetInput::New("tabs.archiveAfterHours", kId, "{")));
}

// M4: spaces, profiles, overlay.respond (window.newPrivate has no payload).

mojom::SpaceIconPtr Emoji(const std::string& value) {
  return mojom::SpaceIcon::NewEmoji(mojom::SpaceIconEmoji::New("emoji", value));
}

TEST(CluBridgeValidatorsTest, SpacesCreateAndUpdate) {
  EXPECT_TRUE(IsValid(*mojom::SpacesCreateInput::New(
      std::nullopt, nullptr, std::nullopt, std::nullopt)));
  EXPECT_TRUE(IsValid(*mojom::SpacesCreateInput::New(
      "Work", Emoji("\U0001F4BC"), mojom::SpacesCreateInputColor::kTeal, kId)));
  EXPECT_FALSE(IsValid(*mojom::SpacesCreateInput::New(
      std::string(241, 'n'), nullptr, std::nullopt, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::SpacesCreateInput::New(
      kBadUtf8, nullptr, std::nullopt, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::SpacesCreateInput::New(
      std::nullopt, Emoji(std::string(17, 'e')), std::nullopt, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::SpacesCreateInput::New(
      std::nullopt, mojom::SpaceIcon::NewEmoji(
                        mojom::SpaceIconEmoji::New("letter", "x")),
      std::nullopt, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::SpacesCreateInput::New(
      std::nullopt, nullptr, std::nullopt, kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::SpacesUpdateInput::New(
      kId, "Work", nullptr, mojom::SpacesUpdateInputColor::kBlue)));
  EXPECT_FALSE(IsValid(*mojom::SpacesUpdateInput::New(
      kNotAnId, "Work", nullptr, std::nullopt)));
}

TEST(CluBridgeValidatorsTest, SpacesReorderDeleteAndSwitch) {
  EXPECT_TRUE(IsValid(*mojom::SpacesReorderInput::New(kId, -5)));
  EXPECT_FALSE(IsValid(*mojom::SpacesReorderInput::New(kId, 1001)));
  EXPECT_FALSE(IsValid(*mojom::SpacesReorderInput::New(kNotAnId, 0)));
  EXPECT_FALSE(IsValid(*mojom::SpacesDeleteInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::SpacesSwitchInput::NewIndex(
      mojom::SpacesSwitchInputIndex::New(8))));
  EXPECT_FALSE(IsValid(*mojom::SpacesSwitchInput::NewIndex(
      mojom::SpacesSwitchInputIndex::New(9))));
  EXPECT_FALSE(IsValid(*mojom::SpacesSwitchInput::NewIndex(
      mojom::SpacesSwitchInputIndex::New(-1))));
  EXPECT_TRUE(IsValid(*mojom::SpacesSwitchInput::NewStep(
      mojom::SpacesSwitchInputStep::New(-1))));
  EXPECT_FALSE(IsValid(*mojom::SpacesSwitchInput::NewStep(
      mojom::SpacesSwitchInputStep::New(2))));
  EXPECT_FALSE(IsValid(*mojom::SpacesSwitchInput::NewSpaceId(
      mojom::SpacesSwitchInputSpaceId::New(kNotAnId))));
}

TEST(CluBridgeValidatorsTest, SpacesSetProfileMenusAndSwipe) {
  EXPECT_TRUE(IsValid(*mojom::SpacesSetProfileInput::New(kId, kId, true)));
  EXPECT_FALSE(
      IsValid(*mojom::SpacesSetProfileInput::New(kId, kNotAnId, std::nullopt)));
  EXPECT_TRUE(IsValid(*mojom::SpacesOpenOverflowInput::New(
      mojom::Anchor::NewPoint(mojom::Point::New(10, 20)))));
  EXPECT_FALSE(IsValid(*mojom::SpacesOpenOverflowInput::New(
      mojom::Anchor::NewPoint(mojom::Point::New(
          std::numeric_limits<double>::infinity(), 20)))));
  EXPECT_TRUE(IsValid(*mojom::SpacesOpenEditorInput::New(
      std::nullopt, mojom::SpacesOpenEditorInputView::kProfiles)));
  EXPECT_FALSE(IsValid(
      *mojom::SpacesOpenEditorInput::New(kNotAnId, std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::SpacesOpenIconMenuInput::New(
      kNotAnId, mojom::Anchor::NewPoint(mojom::Point::New(1, 1)),
      std::nullopt)));
  EXPECT_TRUE(IsValid(*mojom::SpacesSwipeInput::New(
      mojom::SpacesSwipeInputPhase::kUpdate, 40, 0, false)));
  EXPECT_FALSE(IsValid(*mojom::SpacesSwipeInput::New(
      mojom::SpacesSwipeInputPhase::kUpdate,
      std::numeric_limits<double>::quiet_NaN(), 0, false)));
}

TEST(CluBridgeValidatorsTest, ProfilesAndOverlayRespond) {
  EXPECT_TRUE(IsValid(*mojom::ProfilesCreateInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::ProfilesCreateInput::New(std::string(241, 'p'))));
  EXPECT_TRUE(IsValid(*mojom::ProfilesRenameInput::New(kId, "Client")));
  EXPECT_FALSE(IsValid(*mojom::ProfilesRenameInput::New(kNotAnId, "Client")));
  EXPECT_FALSE(IsValid(*mojom::ProfilesRenameInput::New(kId, kBadUtf8)));
  EXPECT_FALSE(IsValid(*mojom::ProfilesDeleteInput::New(kNotAnId)));
  EXPECT_TRUE(IsValid(*mojom::OverlayRespondInput::New(kId, "true")));
  EXPECT_FALSE(IsValid(*mojom::OverlayRespondInput::New(kId, "{")));
  EXPECT_FALSE(IsValid(*mojom::OverlayRespondInput::New(kNotAnId, "true")));
}

// M9: download rows are addressed by a UUID v4 id only, never by a path.
TEST(CluBridgeValidatorsTest, DownloadActTakesOnlyARowId) {
  EXPECT_TRUE(IsValid(*mojom::DownloadActInput::New(
      kId, mojom::DownloadActInputAction::kOpen)));
  EXPECT_TRUE(IsValid(*mojom::DownloadActInput::New(
      kId, mojom::DownloadActInputAction::kReveal)));
  EXPECT_FALSE(IsValid(*mojom::DownloadActInput::New(
      kNotAnId, mojom::DownloadActInputAction::kOpen)));
  EXPECT_FALSE(IsValid(*mojom::DownloadActInput::New(
      "/Users/someone/Downloads/x.dmg", mojom::DownloadActInputAction::kOpen)));
  EXPECT_FALSE(IsValid(*mojom::DownloadActInput::New(
      kBadUtf8, mojom::DownloadActInputAction::kPause)));
}

// M8: settings.open, browsingData.*, import.*, theme.*, shell.openNavHistory (history.open and onboarding.complete
// carry nothing a cap could refuse: no payload, and one bool).

mojom::SpaceThemePtr Theme(std::vector<std::pair<double, double>> colors,
                           double intensity = 0.5,
                           double grain = 0) {
  std::vector<mojom::ThemeColorPtr> list;
  for (const auto& [h, s] : colors) {
    list.push_back(mojom::ThemeColor::New(h, s));
  }
  return mojom::SpaceTheme::New(std::move(list), intensity, grain);
}

TEST(CluBridgeValidatorsTest, ThemeSpaceThemesAndPicker) {
  EXPECT_TRUE(IsValid(*mojom::ThemeSetSpaceThemeInput::New(kId, Theme({{0, 0}, {359.9, 1}}))));
  EXPECT_TRUE(IsValid(*mojom::ThemePreviewSpaceThemeInput::New(kId, Theme({{10, 0.5}}, 1, 1))));
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  std::vector<mojom::SpaceThemePtr> junk;
  junk.push_back(Theme({}));
  junk.push_back(Theme({{1, 0.1}, {2, 0.2}, {3, 0.3}, {4, 0.4}}));
  junk.push_back(Theme({{360, 0.5}}));
  junk.push_back(Theme({{-1, 0.5}}));
  junk.push_back(Theme({{10, 1.01}}));
  junk.push_back(Theme({{nan, 0.5}}));
  junk.push_back(Theme({{10, 0.5}}, 1.5));
  junk.push_back(Theme({{10, 0.5}}, inf));
  junk.push_back(Theme({{10, 0.5}}, 0.5, -1));
  for (mojom::SpaceThemePtr& theme : junk) {
    EXPECT_FALSE(IsValid(*mojom::ThemeSetSpaceThemeInput::New(kId, theme.Clone())));
    EXPECT_FALSE(IsValid(*mojom::ThemePreviewSpaceThemeInput::New(kId, std::move(theme))));
  }
  EXPECT_FALSE(IsValid(*mojom::ThemeSetSpaceThemeInput::New(kNotAnId, Theme({{10, 0.5}}))));
  EXPECT_TRUE(IsValid(*mojom::ThemeResetSpaceThemeInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::ThemeResetSpaceThemeInput::New(kBadUtf8)));
  EXPECT_TRUE(IsValid(*mojom::ThemeOpenPickerInput::New(kId, mojom::Rect::New(1, 2, 3, 4))));
  EXPECT_FALSE(IsValid(*mojom::ThemeOpenPickerInput::New(kId, mojom::Rect::New(nan, 2, 3, 4))));
  EXPECT_FALSE(IsValid(*mojom::ThemeOpenPickerInput::New(kNotAnId, mojom::Rect::New(1, 2, 3, 4))));
}

TEST(CluBridgeValidatorsTest, BrowsingDataAndImport) {
  auto types = [] {
    return mojom::BrowsingDataClearInputTypes::New(true, true, false, false, false, true);
  };
  EXPECT_TRUE(IsValid(*mojom::BrowsingDataClearInput::New(
      kId, mojom::BrowsingDataClearInputRange::kHour, types())));
  EXPECT_FALSE(IsValid(*mojom::BrowsingDataClearInput::New(
      kNotAnId, mojom::BrowsingDataClearInputRange::kHour, types())));
  EXPECT_TRUE(IsValid(*mojom::BrowsingDataOpenInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::BrowsingDataOpenInput::New(kNotAnId)));

  auto chrome = [](const std::string& source, const std::string& dir, const std::string& space) {
    return mojom::ImportRunInput::NewChrome(
        mojom::ImportRunInputChrome::New(source, dir, true, false, true, true, true, false, space));
  };
  EXPECT_TRUE(IsValid(*chrome("chrome", "Default", kId)));
  EXPECT_TRUE(IsValid(*chrome("chrome", "Profile 12", kId)));
  for (const char* dir : {"../Chrome", "Profile 1/../..", "System Profile", "Profile 1234", "", kBadUtf8}) {
    EXPECT_FALSE(IsValid(*chrome("chrome", dir, kId))) << dir;
  }
  EXPECT_FALSE(IsValid(*chrome("arc", "Default", kId)));
  EXPECT_FALSE(IsValid(*chrome("chrome", "Default", kNotAnId)));
  const auto arc = [](const char* source) {
    auto input = mojom::ImportRunInputArc::New();
    input->source = source;
    return mojom::ImportRunInput::NewArc(std::move(input));
  };
  EXPECT_TRUE(IsValid(*arc("arc")));
  EXPECT_FALSE(IsValid(*arc("chrome")));
}

TEST(CluBridgeValidatorsTest, ShellWidthAndNavHistory) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_TRUE(IsValid(*mojom::ShellSetSidebarWidthInput::New(300, true)));
  EXPECT_FALSE(IsValid(*mojom::ShellSetSidebarWidthInput::New(nan, true)));
  EXPECT_TRUE(IsValid(*mojom::ShellOpenNavHistoryInput::New(
      mojom::ShellOpenNavHistoryInputDirection::kBack, mojom::Rect::New(1, 2, 3, 4))));
  EXPECT_FALSE(IsValid(*mojom::ShellOpenNavHistoryInput::New(
      mojom::ShellOpenNavHistoryInputDirection::kForward, mojom::Rect::New(1, nan, 3, 4))));
}

// M6 (docs/chromium/m6.md): split.*, media.*, peek.* and mini.*. The
// cross-field rules zod refines (unique ids, no drop on itself, sizes summing
// to 1) are the handlers' (clu_multitask_handlers.cc), tested in
// CluSplitRulesTest.
TEST(CluBridgeValidatorsTest, SplitCommands) {
  constexpr char kOther[] = "00000000-0000-4000-8000-000600000016";
  EXPECT_TRUE(IsValid(
      *mojom::SplitCreateInput::New(std::vector<std::string>{kId, kOther},
                                    mojom::SplitCreateInputDir::kRow)));
  // One tab, five tabs, a junk id.
  EXPECT_FALSE(IsValid(*mojom::SplitCreateInput::New(
      std::vector<std::string>{kId}, mojom::SplitCreateInputDir::kRow)));
  EXPECT_FALSE(IsValid(*mojom::SplitCreateInput::New(
      std::vector<std::string>(5, kId), mojom::SplitCreateInputDir::kCol)));
  EXPECT_FALSE(IsValid(
      *mojom::SplitCreateInput::New(std::vector<std::string>{kId, kNotAnId},
                                    mojom::SplitCreateInputDir::kRow)));
  EXPECT_TRUE(IsValid(*mojom::SplitDropTabInput::New(
      kId, kOther, mojom::SplitDropTabInputEdge::kLeft)));
  EXPECT_FALSE(IsValid(*mojom::SplitDropTabInput::New(
      kNotAnId, kOther, mojom::SplitDropTabInputEdge::kLeft)));
  EXPECT_FALSE(IsValid(*mojom::SplitSwapInput::New(kId, kBadUtf8)));
  EXPECT_TRUE(IsValid(*mojom::SplitResizeInput::New(
      kId, std::vector<int32_t>{}, std::vector<double>{0.4, 0.6})));
  // NaN, a size of 1, a path too deep, an index past 3, one size.
  EXPECT_FALSE(IsValid(*mojom::SplitResizeInput::New(
      kId, std::vector<int32_t>{}, std::vector<double>{std::nan(""), 0.5})));
  EXPECT_FALSE(IsValid(*mojom::SplitResizeInput::New(
      kId, std::vector<int32_t>{}, std::vector<double>{1, 0.5})));
  EXPECT_FALSE(IsValid(*mojom::SplitResizeInput::New(
      kId, std::vector<int32_t>{0, 0, 0}, std::vector<double>{0.5, 0.5})));
  EXPECT_FALSE(IsValid(*mojom::SplitResizeInput::New(
      kId, std::vector<int32_t>{4}, std::vector<double>{0.5, 0.5})));
  EXPECT_FALSE(IsValid(*mojom::SplitResizeInput::New(
      kId, std::vector<int32_t>{}, std::vector<double>{0.5})));
  EXPECT_FALSE(IsValid(
      *mojom::SplitEqualizeInput::New(kNotAnId, std::vector<int32_t>{})));
  EXPECT_FALSE(IsValid(*mojom::SplitRemovePaneInput::New(
      kNotAnId, mojom::SplitRemovePaneInputMode::kSeparate)));
  EXPECT_TRUE(IsValid(*mojom::SplitFocusPaneInput::NewIndex(
      mojom::SplitFocusPaneInputIndex::New(2))));
  EXPECT_FALSE(IsValid(*mojom::SplitFocusPaneInput::NewIndex(
      mojom::SplitFocusPaneInputIndex::New(5))));
  EXPECT_FALSE(IsValid(*mojom::SplitFocusPaneInput::NewStep(
      mojom::SplitFocusPaneInputStep::New(2))));
  EXPECT_TRUE(IsValid(*mojom::SplitFocusPaneInput::NewStep(
      mojom::SplitFocusPaneInputStep::New(-1))));
  EXPECT_FALSE(IsValid(*mojom::SplitFocusPaneInput::NewTabId(
      mojom::SplitFocusPaneInputTabId::New(kNotAnId))));
  EXPECT_FALSE(IsValid(*mojom::SplitDragStateInput::New(
      mojom::SplitDragStateInputPhase::kHover, kId, kNotAnId, std::nullopt)));
}

TEST(CluBridgeValidatorsTest, MediaPeekAndMiniCommands) {
  EXPECT_TRUE(IsValid(*mojom::MediaToggleInput::New(kId)));
  EXPECT_FALSE(IsValid(*mojom::MediaToggleInput::New(kNotAnId)));
  EXPECT_FALSE(IsValid(*mojom::MediaFocusInput::New(kBadUtf8)));
  EXPECT_TRUE(IsValid(*mojom::PeekAnimationDoneInput::New(3)));
  EXPECT_FALSE(IsValid(*mojom::PeekAnimationDoneInput::New(1.5)));
  EXPECT_FALSE(IsValid(*mojom::PeekAnimationDoneInput::New(-1)));
  EXPECT_TRUE(IsValid(*mojom::MiniOpenInput::New("https://a.test/")));
  EXPECT_TRUE(IsValid(*mojom::MiniOpenInput::New(std::nullopt)));
  EXPECT_FALSE(IsValid(*mojom::MiniOpenInput::New(TooLongUrl())));
  EXPECT_FALSE(IsValid(*mojom::MiniOpenInput::New(kBadUtf8)));
  EXPECT_TRUE(IsValid(*mojom::MiniMoveToSpaceInput::New(kId, true)));
  EXPECT_FALSE(IsValid(*mojom::MiniMoveToSpaceInput::New(kNotAnId, false)));
}

}  // namespace
}  // namespace clu
