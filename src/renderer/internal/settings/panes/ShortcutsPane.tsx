// Shortcuts (docs/chromium/parity-settings.md section 4): every command of the key table with its keys in
// a keycap field, a search over names and keys, click a field to record a new shortcut, conflicts named with Replace,
// a reset per changed row and Reset All Shortcuts. Everything saves to the global `shortcuts.custom` setting, which the
// browser applies to the key table, the menus and the Peek, and the WebUIs to their tooltips and the command bar.
import { useCallback, useEffect, useMemo, useState } from 'react'
import { chordKey, commandTable, formatChord, parseChord, type CommandEntry } from '../../../../shared/commands'
import { chordFromKey, chordOwner, customShortcuts, effectiveTable, isReservedChord, keysOf, SHORTCUTS_KEY, type CustomShortcuts } from '../../../../shared/shortcuts'
import { Dialog } from '../../../common/Dialog'
import { Button } from '../../../common/controls'
import { useSetting } from '../../../settings-sections/data/parts/useSetting'

interface Note { id: string; chord: string; kind: 'reserved' | 'modifier' | 'conflict'; owner?: CommandEntry }

/** The map without `id`'s entry. */
const without = (custom: CustomShortcuts, id: string): CustomShortcuts => Object.fromEntries(Object.entries(custom).filter(([k]) => k !== id))
const same = (a: string, b: string) => chordKey(parseChord(a)) === chordKey(parseChord(b))
const display = (e: CommandEntry | undefined) => (e?.accelerator ? formatChord(e.accelerator) : '---')

/** One row per command id (the table lists a few twice for its window kinds), sorted by name. */
export function shortcutRows(table: readonly CommandEntry[] = commandTable): CommandEntry[] {
  const seen = new Set<string>()
  return table.filter((e) => !seen.has(e.id) && seen.add(e.id)).sort((a, b) => a.title.localeCompare(b.title) || a.id.localeCompare(b.id))
}

const MODS = '⌃⌥⇧⌘'
const WORDS: [RegExp, string][] = [
  [/\b(cmd|command)\b/g, '⌘'],
  [/\b(ctrl|control)\b/g, '⌃'],
  [/\b(opt|option|alt)\b/g, '⌥'],
  [/\bshift\b/g, '⇧']
]

/** Whether a row matches the search: its name, an alias, or its keys typed as symbols (⌘W) or words (cmd w, Cmd+W). */
export function matchesQuery(entry: CommandEntry, query: string): boolean {
  const q = query.trim().toLowerCase()
  if (!q) return true
  if (entry.title.toLowerCase().includes(q) || entry.aliases?.some((a) => a.toLowerCase().includes(q))) return true
  let keys = q
  for (const [re, sym] of WORDS) keys = keys.replace(re, sym)
  keys = keys.replace(/[\s+]/g, '')
  // Keys match when the key and the set of modifiers are the same, in any order (⌘⇧W finds ⇧⌘W, ⌘W does not).
  const split = (s: string) => ({ mods: [...s].filter((ch) => MODS.includes(ch)).sort().join(''), key: [...s].filter((ch) => !MODS.includes(ch)).join('') })
  const typed = split(keys)
  if (!typed.mods || !typed.key) return false
  return keysOf(entry).some((k) => {
    const shown = split(formatChord(k).toLowerCase())
    return shown.mods === typed.mods && shown.key === typed.key
  })
}

export function ShortcutsPane() {
  const { value, error, set } = useSetting(SHORTCUTS_KEY, 'global')
  const custom = customShortcuts(value)
  const effective = useMemo(() => effectiveTable(custom), [custom])
  const byId = useMemo(() => new Map(effective.map((e) => [e.id, e])), [effective])
  const rows = useMemo(() => shortcutRows(), [])
  const [query, setQuery] = useState('')
  const [recording, setRecording] = useState<string | null>(null)
  const [note, setNote] = useState<Note | null>(null)
  const [confirmReset, setConfirmReset] = useState(false)

  const write = useCallback((next: CustomShortcuts) => set(next), [set])

  const assign = useCallback(
    (id: string, chord: string, owner?: CommandEntry) => {
      const own = commandTable.find((e) => e.id === id)
      // The command's own key again restores it as shipped, alternates included.
      const next = own?.accelerator && same(own.accelerator, chord) ? without(custom, id) : { ...custom, [id]: chord }
      // A key another custom entry holds moves here; a key a command has by default leaves it on its own.
      const theirs = owner ? custom[owner.id] : undefined
      if (owner && typeof theirs === 'string' && same(theirs, chord)) next[owner.id] = null
      write(next)
      setNote(null)
    },
    [custom, write]
  )

  const clear = useCallback(
    (id: string) => {
      write(commandTable.find((e) => e.id === id)?.accelerator ? { ...custom, [id]: null } : without(custom, id))
    },
    [custom, write]
  )

  const reset = useCallback(
    (id: string) => {
      write(without(custom, id))
      setNote((n) => (n?.id === id ? null : n))
    },
    [custom, write]
  )

  // While a field records, every key goes to it: Esc cancels, Backspace or Delete clears, a lone Tab moves on.
  useEffect(() => {
    if (!recording) return
    const onKey = (e: KeyboardEvent) => {
      const bare = !e.metaKey && !e.ctrlKey && !e.altKey
      if (bare && e.key === 'Tab') {
        setRecording(null)
        return
      }
      e.preventDefault()
      e.stopPropagation()
      if (bare && !e.shiftKey && e.key === 'Escape') {
        setRecording(null)
        return
      }
      if (bare && !e.shiftKey && (e.key === 'Backspace' || e.key === 'Delete')) {
        setRecording(null)
        setNote(null)
        clear(recording)
        return
      }
      const chord = chordFromKey(e)
      if (!chord) return
      setRecording(null)
      if (bare) return setNote({ id: recording, chord, kind: 'modifier' })
      if (isReservedChord(chord)) return setNote({ id: recording, chord, kind: 'reserved' })
      const owner = chordOwner(chord, recording, effective)
      if (owner) return setNote({ id: recording, chord, kind: 'conflict', owner })
      assign(recording, chord)
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [recording, effective, assign, clear])

  const shown = rows.filter((e) => matchesQuery(byId.get(e.id) ?? e, query))
  const changed = shown.filter((e) => Object.hasOwn(custom, e.id))
  const rest = shown.filter((e) => !Object.hasOwn(custom, e.id))

  const row = (e: CommandEntry) => {
    const now = byId.get(e.id)
    const isRecording = recording === e.id
    const isCustom = Object.hasOwn(custom, e.id)
    const mine = note?.id === e.id ? note : null
    return (
      <li key={e.id} className="clu-sw-sc-row" data-command={e.id} data-custom={isCustom}>
        <span className="clu-sw-sc-title">{e.title}</span>
        {isCustom && (
          <button type="button" className="clu-sw-sc-reset" aria-label={`Reset ${e.title} to ${display(e)}`} title={`Reset to ${display(e)}`} onClick={() => reset(e.id)}>
            <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
              <path d="M2.5 6a3.5 3.5 0 1 0 1-2.5M2.5 1.8v2h2" />
            </svg>
          </button>
        )}
        <button
          type="button"
          className="clu-sw-keycap"
          data-recording={isRecording}
          data-empty={!now?.accelerator}
          aria-label={`${e.title}: ${now?.accelerator ? formatChord(now.accelerator) : 'no shortcut'}. Click to record a new one.`}
          onClick={() => {
            setNote(null)
            setRecording(isRecording ? null : e.id)
          }}
          onBlur={() => setRecording((r) => (r === e.id ? null : r))}
        >
          {isRecording ? 'Type shortcut' : display(now)}
        </button>
        {mine && (
          <div className="clu-sw-sc-note" role="alert">
            {mine.kind === 'modifier' && <span>Use ⌘, ⌃ or ⌥ with the key.</span>}
            {mine.kind === 'reserved' && <span>macOS keeps {formatChord(mine.chord)} for itself.</span>}
            {mine.kind === 'conflict' && mine.owner && (
              <>
                <span>
                  {formatChord(mine.chord)} is used by {mine.owner.title}.
                </span>
                <Button onClick={() => assign(e.id, mine.chord, mine.owner)}>Replace</Button>
                <Button variant="plain" onClick={() => setNote(null)}>
                  Cancel
                </Button>
              </>
            )}
          </div>
        )}
      </li>
    )
  }

  return (
    <div className="clu-sw-shortcuts">
      <section className="clu-group clu-sw-sc-list" aria-label="Shortcuts">
        <label className="clu-sw-sc-search">
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
            <circle cx="7" cy="7" r="4.6" />
            <path d="m10.4 10.4 3.4 3.4" />
          </svg>
          <input type="search" value={query} placeholder="Type a feature name or shortcut…" aria-label="Search shortcuts" onChange={(e) => setQuery(e.currentTarget.value)} />
        </label>
        <div className="clu-sw-sc-scroll">
          {changed.length > 0 && (
            <>
              <h3 className="clu-sw-sc-group">Custom Shortcuts</h3>
              <ul aria-label="Custom shortcuts">{changed.map(row)}</ul>
            </>
          )}
          {rest.length > 0 && (
            <>
              <h3 className="clu-sw-sc-group">Default Shortcuts</h3>
              <ul aria-label="Default shortcuts">{rest.map(row)}</ul>
            </>
          )}
          {shown.length === 0 && <p className="clu-sw-sc-empty">No action matches “{query.trim()}”.</p>}
        </div>
      </section>
      <aside className="clu-sw-sc-side">
        <div className="clu-sw-sc-art" aria-hidden="true">
          <span>⌘</span>
          <span>
            <svg width="32" height="32" viewBox="0 0 32 32" fill="none" stroke="currentColor" strokeWidth="2.6" strokeLinecap="round">
              <circle cx="16" cy="16" r="12.5" />
              <path d="M11.5 13v1M20.5 13v1M10.5 19.5c3 3.4 8 3.4 11 0" />
            </svg>
          </span>
        </div>
        <h2>Custom Shortcuts</h2>
        <p>Change shortcuts for your favorite actions. Click a shortcut to type a new one; it works in every Rio window.</p>
        {error && (
          <p className="clu-settings-error" role="alert">
            {error}
          </p>
        )}
        <Button className="clu-sw-sc-reset-all" disabled={Object.keys(custom).length === 0} onClick={() => setConfirmReset(true)}>
          Reset All Shortcuts
        </Button>
      </aside>
      {confirmReset && (
        <div className="clu-sw-scrim">
          <Dialog title="Reset all shortcuts?" onCancel={() => setConfirmReset(false)}>
            <p className="clu-dialog-body">Every action gets its own shortcut back, and your changes are removed.</p>
            <div className="clu-dialog-actions">
              <Button onClick={() => setConfirmReset(false)}>Cancel</Button>
              <Button
                variant="primary"
                onClick={() => {
                  setConfirmReset(false)
                  setNote(null)
                  write({})
                }}
              >
                Reset All
              </Button>
            </div>
          </Dialog>
        </div>
      )}
    </div>
  )
}
