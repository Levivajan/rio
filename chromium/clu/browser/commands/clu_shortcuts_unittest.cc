// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/commands/clu_shortcuts.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/test/bind.h"
#include "clu/browser/commands/command_table.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace clu {
namespace {

constexpr int kCmd = ui::EF_COMMAND_DOWN;
constexpr int kShift = ui::EF_SHIFT_DOWN;
constexpr int kCtrl = ui::EF_CONTROL_DOWN;
constexpr int kAlt = ui::EF_ALT_DOWN;

// The small table of src/shared/shortcuts.test.ts.
constexpr CommandAlt kOneAlts[] = {
    {{ui::VKEY_K, kCmd | kShift}, CommandRoute::kRouter}};
constexpr CommandSpec kTable[] = {
    {1, "a.one", "One", CommandMenu::kFile, CommandVariant::kMain,
     CommandRoute::kRouter, true, true, {ui::VKEY_K, kCmd}, kOneAlts},
    {2, "a.two", "Two", CommandMenu::kFile, CommandVariant::kMain,
     CommandRoute::kMenu, true, true, {ui::VKEY_J, kCmd}, {}},
    {3, "a.mini", "Mini", CommandMenu::kFile, CommandVariant::kMini,
     CommandRoute::kRouter, false, true, {ui::VKEY_O, kCmd}, {}},
    {4, "a.both", "Both", CommandMenu::kFile, CommandVariant::kBoth,
     CommandRoute::kRouter, false, true, {ui::VKEY_L, kCmd}, {}},
    {5, "a.none", "None", CommandMenu::kFile, CommandVariant::kMain,
     CommandRoute::kMenu, true, false, {ui::VKEY_UNKNOWN, ui::EF_NONE}, {}},
};

base::DictValue Json(std::string_view json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  CHECK(value && value->is_dict()) << json;
  return std::move(*value).TakeDict();
}

// "K/C" style labels of each entry's keys: the primary first, then the alternates.
std::vector<std::vector<std::string>> Keys(std::string_view custom) {
  std::vector<std::vector<std::string>> out;
  for (const CommandKeys& keys : ResolveShortcuts(kTable, Json(custom))) {
    std::vector<std::string> row;
    auto label = [](const CommandAccelerator& a) {
      return std::to_string(a.key) + "/" + std::to_string(a.modifiers);
    };
    if (keys.primary) {
      row.push_back(label(*keys.primary));
    }
    for (const CommandAlt& alt : keys.alts) {
      row.push_back(label(alt.accelerator));
    }
    out.push_back(row);
  }
  return out;
}

std::string L(ui::KeyboardCode key, int modifiers) {
  return std::to_string(key) + "/" + std::to_string(modifiers);
}

TEST(CluShortcutsTest, ParsesTheTableSyntax) {
  auto is = [](std::string_view chord, ui::KeyboardCode key, int modifiers) {
    std::optional<CommandAccelerator> a = ParseChord(chord);
    return a && a->key == key && a->modifiers == modifiers;
  };
  EXPECT_TRUE(is("Cmd+Shift+]", ui::VKEY_OEM_6, kCmd | kShift));
  EXPECT_TRUE(is("Ctrl+Shift+=", ui::VKEY_OEM_PLUS, kCtrl | kShift));
  EXPECT_TRUE(is("Cmd+Plus", ui::VKEY_OEM_PLUS, kCmd | kShift));
  EXPECT_TRUE(is("Cmd++", ui::VKEY_OEM_PLUS, kCmd | kShift));
  EXPECT_TRUE(is("Cmd+Option+1", ui::VKEY_1, kCmd | kAlt));
  EXPECT_TRUE(is("Shift+Cmd+K", ui::VKEY_K, kCmd | kShift));
  EXPECT_TRUE(is("Cmd+Backspace", ui::VKEY_BACK, kCmd));
  EXPECT_TRUE(is("Ctrl+`", ui::VKEY_OEM_3, kCtrl));
  for (std::string_view junk : {"", "Cmd+", "Hyper+K", "Cmd+F13", "+K", "Cmd+k"}) {
    EXPECT_FALSE(ParseChord(junk)) << junk;
  }
}

TEST(CluShortcutsTest, AssignableChordsNeedAModifierAndAreNotMacOSs) {
  for (std::string_view ok : {"Cmd+K", "Ctrl+Shift+=", "Option+Left",
                              "Cmd+Option+1", "Ctrl+`", "Cmd+Plus"}) {
    EXPECT_TRUE(IsAssignableChord(ok)) << ok;
  }
  for (std::string_view no : {"K", "Shift+K", "Cmd+Q", "Cmd+Tab", "Cmd+C",
                              "Cmd+Shift+Z", "Cmd+KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK"}) {
    EXPECT_FALSE(IsAssignableChord(no)) << no;
  }
}

TEST(CluShortcutsTest, ValidatesTheMap) {
  EXPECT_TRUE(IsValidCustomShortcuts(base::Value(base::DictValue())));
  EXPECT_TRUE(IsValidCustomShortcuts(base::Value(Json(
      R"({"tabs.close": null, "nav.back": "Cmd+[", "unknown.id": "Cmd+K"})"))));
  EXPECT_FALSE(IsValidCustomShortcuts(base::Value(Json(R"({"tabs.close": "Cmd+Q"})"))));
  EXPECT_FALSE(IsValidCustomShortcuts(base::Value(Json(R"({"tabs.close": 3})"))));
  EXPECT_FALSE(IsValidCustomShortcuts(base::Value(Json(R"({"": "Cmd+K"})"))));
  EXPECT_FALSE(IsValidCustomShortcuts(base::Value(base::ListValue())));
  EXPECT_FALSE(IsValidCustomShortcuts(base::Value()));
  base::DictValue big;
  for (int i = 0; i < 257; ++i) {
    big.Set("c." + std::to_string(i), base::Value());
  }
  EXPECT_FALSE(IsValidCustomShortcuts(base::Value(std::move(big))));
}

TEST(CluShortcutsTest, IsTheTableWithoutCustomKeys) {
  EXPECT_EQ(Keys("{}"),
            (std::vector<std::vector<std::string>>{
                {L(ui::VKEY_K, kCmd), L(ui::VKEY_K, kCmd | kShift)},
                {L(ui::VKEY_J, kCmd)},
                {L(ui::VKEY_O, kCmd)},
                {L(ui::VKEY_L, kCmd)},
                {}}));
}

TEST(CluShortcutsTest, ReplacesAChordAndItsAlternatesAndNullLeavesNone) {
  const auto keys = Keys(R"({"a.one": "Ctrl+Shift+=", "a.two": null})");
  EXPECT_EQ(keys[0], (std::vector<std::string>{L(ui::VKEY_OEM_PLUS, kCtrl | kShift)}));
  EXPECT_TRUE(keys[1].empty());
}

TEST(CluShortcutsTest, ATakenChordLeavesItsDefaultOwnerWhereBothCanRun) {
  EXPECT_TRUE(Keys(R"({"a.none": "Cmd+J"})")[1].empty());
  EXPECT_EQ(Keys(R"({"a.none": "Cmd+J"})")[4],
            (std::vector<std::string>{L(ui::VKEY_J, kCmd)}));
  // The alternate goes, the main chord stays.
  EXPECT_EQ(Keys(R"({"a.none": "Cmd+Shift+K"})")[0],
            (std::vector<std::string>{L(ui::VKEY_K, kCmd)}));
  // Main and mini never meet.
  EXPECT_EQ(Keys(R"({"a.none": "Cmd+O"})")[2],
            (std::vector<std::string>{L(ui::VKEY_O, kCmd)}));
  // Both meets either.
  EXPECT_TRUE(Keys(R"({"a.mini": "Cmd+L"})")[3].empty());
}

TEST(CluShortcutsTest, TheFirstOfTwoCustomEntriesKeepsTheChord) {
  const auto keys = Keys(R"({"a.none": "Cmd+Y", "a.two": "Cmd+Y"})");
  EXPECT_EQ(keys[1], (std::vector<std::string>{L(ui::VKEY_Y, kCmd)}));
  EXPECT_TRUE(keys[4].empty());
  const auto plus = Keys(R"({"a.none": "Ctrl+Plus", "a.two": "Ctrl+Shift+="})");
  EXPECT_FALSE(plus[1].empty());
  EXPECT_TRUE(plus[4].empty());
}

TEST(CluShortcutsTest, IgnoresUnknownIdsAndUnusableChords) {
  const auto keys =
      Keys(R"({"gone.command": "Cmd+K", "a.two": "Cmd+Q", "a.one": "K"})");
  EXPECT_EQ(keys[0].size(), 2u);
  EXPECT_EQ(keys[1], (std::vector<std::string>{L(ui::VKEY_J, kCmd)}));
}

TEST(CluShortcutsTest, KeysForFollowsTheProcessMapAndTellsListeners) {
  ResetShortcutsForTesting();
  const CommandSpec* close = nullptr;
  const CommandSpec* history = nullptr;
  for (const CommandSpec& spec : CommandTable()) {
    if (std::string_view(spec.id) == "tabs.close") {
      close = &spec;
    } else if (std::string_view(spec.id) == "history.open") {
      history = &spec;
    }
  }
  ASSERT_TRUE(close && history);
  ASSERT_TRUE(KeysFor(*close).primary);
  EXPECT_EQ(KeysFor(*close).primary->key, ui::VKEY_W);

  int changes = 0;
  base::CallbackListSubscription sub = AddShortcutsChangedCallback(
      base::BindLambdaForTesting([&changes] { ++changes; }));
  SetCustomShortcuts(Json(R"({"tabs.close": "Cmd+Y"})"));
  EXPECT_EQ(changes, 1);
  EXPECT_EQ(KeysFor(*close).primary->key, ui::VKEY_Y);
  // History lost Cmd+Y to the custom key.
  EXPECT_FALSE(KeysFor(*history).primary);
  // The same map again changes nothing.
  SetCustomShortcuts(Json(R"({"tabs.close": "Cmd+Y"})"));
  EXPECT_EQ(changes, 1);
  SetCustomShortcuts(base::DictValue());
  EXPECT_EQ(changes, 2);
  EXPECT_EQ(KeysFor(*close).primary->key, ui::VKEY_W);
  ResetShortcutsForTesting();
}

TEST(CluShortcutsTest, TheShippedTableHasNoChordTwoMeetingCommandsShare) {
  struct Owned {
    CommandAccelerator accelerator;
    CommandVariant variant;
    std::string_view id;
  };
  std::vector<Owned> all;
  for (const CommandSpec& spec : CommandTable()) {
    if (spec.has_accelerator) {
      all.push_back({spec.accelerator, spec.variant, spec.id});
    }
    for (const CommandAlt& alt : spec.alts) {
      all.push_back({alt.accelerator, spec.variant, spec.id});
    }
  }
  auto meet = [](CommandVariant a, CommandVariant b) {
    return a == CommandVariant::kBoth || b == CommandVariant::kBoth || a == b;
  };
  for (size_t i = 0; i < all.size(); ++i) {
    for (size_t j = i + 1; j < all.size(); ++j) {
      if (all[i].id != all[j].id && meet(all[i].variant, all[j].variant)) {
        EXPECT_FALSE(all[i].accelerator.key == all[j].accelerator.key &&
                     all[i].accelerator.modifiers ==
                         all[j].accelerator.modifiers)
            << all[i].id << " and " << all[j].id;
      }
    }
  }
}

}  // namespace
}  // namespace clu
