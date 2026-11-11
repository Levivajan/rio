import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { commandEntry, commandTable, type CommandEntry } from '../commands'
import { entry } from '../commands/define'
import type { ActionRow } from '../ipc/command'
import { ACTION_MIN_SCORE, cleanActionTitle, matchActions, MAX_ACTION_TITLE, paletteRows } from './actions'
import { matchText } from './match'

const registry = (enabled: Iterable<string>, titles: Record<string, string> = {}) => {
  const on = new Set(enabled)
  return { isEnabled: (id: string) => on.has(id), title: (id: string) => titles[id] ?? commandEntry(id)?.title ?? id }
}

describe('paletteRows', () => {
  it('every palette entry with a handler yields one row with its title, aliases and shortcut hint', () => {
    const palette = commandTable.filter((e) => e.palette)
    const rows = paletteRows(registry(commandTable.map((e) => e.id)))
    expect(rows.map((r) => r.id)).toEqual(palette.map((e) => e.id))
    for (const e of palette) {
      const row = rows.find((r) => r.id === e.id)
      expect(row).toEqual({ id: e.id, title: e.title, ...(e.aliases?.length ? { aliases: e.aliases } : {}), ...(e.accelerator ? { accelerator: e.accelerator } : {}) })
    }
  })

  it('hides an entry without a handler and a non-palette entry with one, and uses runtime titles', () => {
    const table: CommandEntry[] = [
      entry({ id: 'a.one', title: 'One', aliases: ['First'], accelerator: 'Cmd+1', menu: 'File', route: 'menu', palette: true }),
      entry({ id: 'a.two', title: 'Two', menu: 'File', route: 'menu', palette: true }),
      entry({ id: 'a.three', title: 'Three', menu: 'File', route: 'menu' })
    ]
    expect(paletteRows(registry(['a.one', 'a.three'], { 'a.one': 'Uno' }), table)).toEqual([{ id: 'a.one', title: 'Uno', aliases: ['First'], accelerator: 'Cmd+1' }])
    expect(paletteRows(registry([]), table)).toEqual([])
  })

  // 04-command F8 rows and where each comes from now: a table entry (any lane's module) or a provider.
  const F8: Record<string, string> = {
    'sidebar.toggle': 'sidebar.toggle',
    'tab.copyUrl': 'edit.copyUrl',
    'space.create': 'spaces.newSpace',
    'split.add': 'split.add',
    'settings.open': 'settings.open',
    'history.open': 'history.open',
    'history.clear': 'browsingData.open',
    'tab.reload': 'nav.reload',
    'tab.close': 'tabs.close',
    'tab.reopenClosed': 'archive.reopenClosed',
    'tab.pinToggle': 'tabs.togglePin',
    'window.new': 'window.new'
  }

  it('the F8 rows are command table entries', () => {
    expect(Object.keys(F8).length + 1).toBe(13)
    // spaces.newSpace is added by the spaces lane's own table module, which merges separately.
    const pending = commandEntry('spaces.newSpace') ? [] : ['space.create']
    for (const [row, id] of Object.entries(F8)) {
      if (pending.includes(row)) continue
      expect(commandEntry(id), `${row} -> ${id}`).toBeDefined()
    }
  })

  it.skipIf(!commandEntry('spaces.newSpace'))('New Space is a palette entry once the spaces table module defines it', () => {
    expect(commandEntry('spaces.newSpace')?.palette).toBe(true)
  })

  // File ▸ Capture… is a palette entry: 'capture' and 'screenshot' find it with its hint.
  it('Capture… comes up for capture and screenshot with the Cmd+Shift+2 hint', () => {
    const rows = paletteRows(registry(commandTable.map((e) => e.id)))
    for (const q of ['capture', 'screenshot']) expect(matchActions(rows, q)[0], q).toMatchObject({ id: 'page.capture', title: 'Capture…', accelerator: 'Cmd+Shift+2' })
    // The menu-only Help and app menu items stay out of the palette (the bar has its own Contact the Team row).
    expect(rows.map((r) => r.id)).not.toContain('platform.contactTeam')
    expect(rows.map((r) => r.id)).not.toContain('platform.setDefaultBrowser')
  })

  it('actions.ts holds no second list of actions', () => {
    const src = readFileSync(new URL('./actions.ts', import.meta.url), 'utf8')
    for (const e of commandTable) {
      expect(src.includes(`'${e.id}'`), e.id).toBe(false)
      expect(src.includes(`'${e.title}'`), e.title).toBe(false)
    }
  })
})

describe('matchActions', () => {
  const rows: ActionRow[] = [
    { id: 'sidebar.toggle', title: 'Toggle Sidebar', accelerator: 'Cmd+S' },
    { id: 'settings.open', title: 'Settings…', aliases: ['Preferences'] },
    { id: 'search.engine', providerId: 'search.engine', token: 'duckduckgo', title: 'Set search engine: DuckDuckGo' },
    { id: 'zoom.in', title: 'Zoom In' }
  ]
  const ids = (q: string) => matchActions(rows, q).map((r) => r.token ?? r.id)

  it('an empty query lists every row in order', () => {
    expect(ids('')).toEqual(['sidebar.toggle', 'settings.open', 'duckduckgo', 'zoom.in'])
    expect(ids('   ')).toEqual(['sidebar.toggle', 'settings.open', 'duckduckgo', 'zoom.in'])
  })

  it('matches titles and aliases at the 0.6 threshold, best first', () => {
    expect(ids('sidebar')).toEqual(['sidebar.toggle'])
    expect(ids('prefer')).toEqual(['settings.open'])
    expect(ids('duck')).toEqual(['duckduckgo'])
    expect(ids('set')).toEqual(['settings.open', 'duckduckgo'])
    // A loose subsequence (under 0.6) is not an action hit.
    expect(matchText('Toggle Sidebar', 'tgsb')).toBeLessThan(ACTION_MIN_SCORE)
    expect(ids('tgsb')).toEqual([])
  })

  it('marks a row whose title or alias is the whole query as exact and puts it first', () => {
    expect(matchActions(rows, 'toggle sidebar')[0]).toEqual({ ...rows[0], exact: true })
    expect(matchActions(rows, 'Settings…')[0]).toMatchObject({ id: 'settings.open', exact: true })
    expect(matchActions(rows, 'PREFERENCES')[0]).toMatchObject({ id: 'settings.open', exact: true })
    expect(matchActions(rows, 'toggle')[0]?.exact).toBeUndefined()
  })

  it('never throws on junk queries and caps the result', () => {
    expect(() => matchActions(rows, '\ud800'.repeat(5000))).not.toThrow()
    const many = Array.from({ length: 80 }, (_, i) => ({ id: `x${i}`, title: `Item ${i}` }))
    expect(matchActions(many, 'item')).toHaveLength(50)
  })
})

describe('cleanActionTitle', () => {
  it('keeps one clean line and drops junk', () => {
    expect(cleanActionTitle('  Switch to\nWork\u0007 ')).toBe('Switch to Work')
    expect(cleanActionTitle('x'.repeat(1000))).toHaveLength(MAX_ACTION_TITLE)
    expect(cleanActionTitle('\ud800a')).toBe('�a')
    for (const junk of [null, undefined, 42, {}, '', '   ']) expect(cleanActionTitle(junk)).toBeNull()
  })
})
