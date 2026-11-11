// Copyright 2026 The Rio Authors. All rights reserved.

#ifndef CLU_BROWSER_COMMANDS_CLU_SHORTCUTS_H_
#define CLU_BROWSER_COMMANDS_CLU_SHORTCUTS_H_

#include <optional>
#include <string_view>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/span.h"
#include "base/functional/callback_forward.h"
#include "base/values.h"
#include "clu/browser/commands/command_table.h"

// Custom shortcuts (docs/chromium/parity-settings.md section 4), the port of src/shared/shortcuts.ts with the same
// cases. The global setting `shortcuts.custom` maps a command id to a chord in the table's syntax ("Cmd+Shift+K") or
// null (no key). One resolution gives each table command the keys it answers to now: a custom entry replaces the
// command's chord and its alternates; a command without one keeps its own, minus any chord a custom entry took where
// both can run (main and both, mini and both); of two custom entries on one chord the first in table order keeps it.
// CluAppSettings feeds the map in; the key table (CluShell), the main menu and a focused Peek read KeysFor().
namespace clu {

inline constexpr char kShortcutsCustomKey[] = "shortcuts.custom";

// "Cmd+Shift+]" as a key and ui::EF_* modifiers, or nullopt for a chord the table syntax cannot name.
std::optional<CommandAccelerator> ParseChord(std::string_view chord);

// Keys macOS keeps for itself (Cmd+Q, Cmd+H, Cmd+Tab, the clipboard, undo, ...).
bool IsReservedChord(const CommandAccelerator& accelerator);

// A chord a user may give a command: it parses, has Cmd, Ctrl or Option, and is not one macOS keeps.
bool IsAssignableChord(std::string_view chord);

// The registry's validator: a dict of at most 256 entries whose values are none or an assignable chord. Unknown
// command ids pass and are ignored, so a later table change keeps the user's keys.
bool IsValidCustomShortcuts(const base::Value& value);

struct CommandKeys {
  CommandKeys();
  CommandKeys(const CommandKeys&);
  CommandKeys& operator=(const CommandKeys&);
  ~CommandKeys();

  std::optional<CommandAccelerator> primary;
  std::vector<CommandAlt> alts;
};

// The keys of each entry of `table`, in its order, with `custom` applied.
std::vector<CommandKeys> ResolveShortcuts(base::span<const CommandSpec> table,
                                          const base::DictValue& custom);

// The process's custom map (CluAppSettings sets it from Local State and on every settings.set of the key). Callbacks
// run when it changes what any command answers to.
void SetCustomShortcuts(const base::DictValue& custom);
base::CallbackListSubscription AddShortcutsChangedCallback(
    base::RepeatingClosure callback);

// The keys `spec` (an entry of CommandTable()) answers to now. An entry from another table gets its own keys.
CommandKeys KeysFor(const CommandSpec& spec);

void ResetShortcutsForTesting();

}  // namespace clu

#endif  // CLU_BROWSER_COMMANDS_CLU_SHORTCUTS_H_
