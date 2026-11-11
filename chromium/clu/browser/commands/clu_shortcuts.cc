// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/commands/clu_shortcuts.h"

#include <string>
#include <utility>

#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace clu {

namespace {

constexpr size_t kMaxEntries = 256;
constexpr size_t kMaxId = 64;
constexpr size_t kMaxChord = 32;

struct NamedKey {
  std::string_view name;
  ui::KeyboardCode key;
};
// src/shared/commands.ts NAMED_CODES, with gen-mojom.ts's key codes.
constexpr NamedKey kNamedKeys[] = {
    {"[", ui::VKEY_OEM_4},       {"]", ui::VKEY_OEM_6},
    {"=", ui::VKEY_OEM_PLUS},    {"-", ui::VKEY_OEM_MINUS},
    {".", ui::VKEY_OEM_PERIOD},  {",", ui::VKEY_OEM_COMMA},
    {"/", ui::VKEY_OEM_2},       {";", ui::VKEY_OEM_1},
    {"'", ui::VKEY_OEM_7},       {"`", ui::VKEY_OEM_3},
    {"\\", ui::VKEY_OEM_5},      {"Left", ui::VKEY_LEFT},
    {"Right", ui::VKEY_RIGHT},   {"Up", ui::VKEY_UP},
    {"Down", ui::VKEY_DOWN},     {"Tab", ui::VKEY_TAB},
    {"Backspace", ui::VKEY_BACK}, {"Enter", ui::VKEY_RETURN},
    {"Escape", ui::VKEY_ESCAPE}, {"Esc", ui::VKEY_ESCAPE},
    {"Space", ui::VKEY_SPACE},
};

constexpr std::string_view kReservedChords[] = {
    "Cmd+Q", "Cmd+H", "Cmd+Option+H", "Cmd+M", "Cmd+Tab",     "Cmd+Space",
    "Cmd+C", "Cmd+V", "Cmd+X",        "Cmd+A", "Cmd+Z",       "Cmd+Shift+Z",
    "Cmd+`",
};

bool Same(const CommandAccelerator& a, const CommandAccelerator& b) {
  return a.key == b.key && a.modifiers == b.modifiers;
}

bool Meet(CommandVariant a, CommandVariant b) {
  return a == CommandVariant::kBoth || b == CommandVariant::kBoth || a == b;
}

struct Store {
  base::DictValue custom;
  std::vector<CommandKeys> resolved;
  base::RepeatingClosureList callbacks;
};

Store& GetStore() {
  static base::NoDestructor<Store> store;
  return *store;
}

const std::vector<CommandKeys>& Resolved() {
  Store& store = GetStore();
  if (store.resolved.empty()) {
    store.resolved = ResolveShortcuts(CommandTable(), store.custom);
  }
  return store.resolved;
}

bool SameKeys(const std::vector<CommandKeys>& a,
              const std::vector<CommandKeys>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].primary.has_value() != b[i].primary.has_value() ||
        (a[i].primary && !Same(*a[i].primary, *b[i].primary)) ||
        a[i].alts.size() != b[i].alts.size()) {
      return false;
    }
    for (size_t j = 0; j < a[i].alts.size(); ++j) {
      if (!Same(a[i].alts[j].accelerator, b[i].alts[j].accelerator) ||
          a[i].alts[j].route != b[i].alts[j].route) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

CommandKeys::CommandKeys() = default;
CommandKeys::CommandKeys(const CommandKeys&) = default;
CommandKeys& CommandKeys::operator=(const CommandKeys&) = default;
CommandKeys::~CommandKeys() = default;

std::optional<CommandAccelerator> ParseChord(std::string_view chord) {
  std::vector<std::string_view> parts = base::SplitStringPiece(
      chord, "+", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.empty()) {
    return std::nullopt;
  }
  std::string_view key = parts.back();
  parts.pop_back();
  // "Cmd+=" and "Ctrl+Shift+-" split cleanly; a trailing empty part means the key was '+'.
  if (key.empty() && !parts.empty() && parts.back().empty()) {
    parts.pop_back();
    key = "Plus";
  }
  int modifiers = ui::EF_NONE;
  for (std::string_view mod : parts) {
    if (mod == "Cmd") {
      modifiers |= ui::EF_COMMAND_DOWN;
    } else if (mod == "Ctrl") {
      modifiers |= ui::EF_CONTROL_DOWN;
    } else if (mod == "Shift") {
      modifiers |= ui::EF_SHIFT_DOWN;
    } else if (mod == "Option" || mod == "Alt") {
      modifiers |= ui::EF_ALT_DOWN;
    } else {
      return std::nullopt;
    }
  }
  if (key == "Plus") {
    return CommandAccelerator{ui::VKEY_OEM_PLUS, modifiers | ui::EF_SHIFT_DOWN};
  }
  if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
    return CommandAccelerator{
        static_cast<ui::KeyboardCode>(ui::VKEY_A + (key[0] - 'A')), modifiers};
  }
  if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') {
    return CommandAccelerator{
        static_cast<ui::KeyboardCode>(ui::VKEY_0 + (key[0] - '0')), modifiers};
  }
  for (const NamedKey& named : kNamedKeys) {
    if (named.name == key) {
      return CommandAccelerator{named.key, modifiers};
    }
  }
  return std::nullopt;
}

bool IsReservedChord(const CommandAccelerator& accelerator) {
  for (std::string_view reserved : kReservedChords) {
    std::optional<CommandAccelerator> parsed = ParseChord(reserved);
    if (parsed && Same(*parsed, accelerator)) {
      return true;
    }
  }
  return false;
}

bool IsAssignableChord(std::string_view chord) {
  if (chord.empty() || chord.size() > kMaxChord) {
    return false;
  }
  std::optional<CommandAccelerator> parsed = ParseChord(chord);
  if (!parsed || !(parsed->modifiers & (ui::EF_COMMAND_DOWN |
                                        ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN))) {
    return false;
  }
  return !IsReservedChord(*parsed);
}

bool IsValidCustomShortcuts(const base::Value& value) {
  if (!value.is_dict() || value.GetDict().size() > kMaxEntries) {
    return false;
  }
  for (const auto [id, chord] : value.GetDict()) {
    if (id.empty() || id.size() > kMaxId) {
      return false;
    }
    if (!chord.is_none() &&
        !(chord.is_string() && IsAssignableChord(chord.GetString()))) {
      return false;
    }
  }
  return true;
}

std::vector<CommandKeys> ResolveShortcuts(base::span<const CommandSpec> table,
                                          const base::DictValue& custom) {
  struct Claim {
    CommandAccelerator accelerator;
    CommandVariant variant;
  };
  std::vector<Claim> claims;
  auto taken = [&claims](const CommandAccelerator& accelerator,
                         CommandVariant variant) {
    for (const Claim& claim : claims) {
      if (Same(claim.accelerator, accelerator) &&
          Meet(claim.variant, variant)) {
        return true;
      }
    }
    return false;
  };

  // Per entry: not customized, customized to none, or customized to a chord.
  enum class Own { kTable, kNone, kChord };
  std::vector<Own> own(table.size(), Own::kTable);
  std::vector<CommandAccelerator> chords(table.size());
  for (size_t i = 0; i < table.size(); ++i) {
    const base::Value* value = custom.Find(table[i].id);
    if (!value) {
      continue;
    }
    if (value->is_none()) {
      own[i] = Own::kNone;
      continue;
    }
    if (!value->is_string() || !IsAssignableChord(value->GetString())) {
      continue;
    }
    const CommandAccelerator accelerator = *ParseChord(value->GetString());
    if (taken(accelerator, table[i].variant)) {
      own[i] = Own::kNone;
      continue;
    }
    claims.push_back({accelerator, table[i].variant});
    own[i] = Own::kChord;
    chords[i] = accelerator;
  }

  std::vector<CommandKeys> out(table.size());
  for (size_t i = 0; i < table.size(); ++i) {
    const CommandSpec& spec = table[i];
    if (own[i] == Own::kChord) {
      out[i].primary = chords[i];
      continue;
    }
    if (own[i] == Own::kNone) {
      continue;
    }
    if (spec.has_accelerator && !taken(spec.accelerator, spec.variant)) {
      out[i].primary = spec.accelerator;
    }
    for (const CommandAlt& alt : spec.alts) {
      if (!taken(alt.accelerator, spec.variant)) {
        out[i].alts.push_back(alt);
      }
    }
  }
  return out;
}

void SetCustomShortcuts(const base::DictValue& custom) {
  Store& store = GetStore();
  if (store.custom == custom && !store.resolved.empty()) {
    return;
  }
  std::vector<CommandKeys> next = ResolveShortcuts(CommandTable(), custom);
  const bool changed = !SameKeys(next, Resolved());
  store.custom = custom.Clone();
  store.resolved = std::move(next);
  if (changed) {
    store.callbacks.Notify();
  }
}

base::CallbackListSubscription AddShortcutsChangedCallback(
    base::RepeatingClosure callback) {
  return GetStore().callbacks.Add(std::move(callback));
}

CommandKeys KeysFor(const CommandSpec& spec) {
  const base::span<const CommandSpec> table = CommandTable();
  const int index = spec.idc - IDC_CLU_FIRST;
  if (index >= 0 && static_cast<size_t>(index) < table.size() &&
      std::string_view(table[index].id) == spec.id) {
    return Resolved()[index];
  }
  CommandKeys keys;
  if (spec.has_accelerator) {
    keys.primary = spec.accelerator;
  }
  keys.alts.assign(spec.alts.begin(), spec.alts.end());
  return keys;
}

void ResetShortcutsForTesting() {
  Store& store = GetStore();
  store.custom = base::DictValue();
  store.resolved.clear();
}

}  // namespace clu
