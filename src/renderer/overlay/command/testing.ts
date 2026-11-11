// Test fixtures for the overlay command bar: a window with a command session, its space and profile,
// optional tabs, and a stub window.clu whose syncIndex, suggest and actions answers the test chooses.
import { act } from '@testing-library/react'
import type { ActionRow, CommandMode, IndexRow } from '../../../shared/ipc/command'
import { defaultProfileState, defaultSpaceState, defaultState, defaultTabState, defaultWindowState, type CluState } from '../../../shared/state'
import { stubClu, type Calls } from '../../../test-utils/stubClu'

export const WIN = '00000000-0000-4000-8000-000000000001'
export const SPACE = '00000000-0000-4000-8000-0000000000a1'
export const PROFILE = '00000000-0000-4000-8000-0000000000b1'
export const SESSION = '00000000-0000-4000-8000-0000000000c1'
export const tabId = (n: number) => `00000000-0000-4000-8000-${String(n).padStart(12, '0')}`

export interface TabSpec { n: number; url: string; title: string; favicon?: string | null; splitId?: string | null; lastActiveAt?: number }

export function barState(opts: { mode?: CommandMode; tabs?: TabSpec[]; activeTab?: number; initialText?: string; targetTab?: number } = {}): CluState {
  const s = defaultState()
  s.profiles[PROFILE] = defaultProfileState({ id: PROFILE, name: 'One', isDefault: true, ephemeral: false })
  // Every tab is a Today row of the space, as the browser lists them.
  s.spaces[SPACE] = {
    ...defaultSpaceState({ id: SPACE, name: 'Home', icon: { kind: 'letter' }, color: 'blue', profileId: PROFILE, ephemeral: false }),
    today: (opts.tabs ?? []).map((t) => tabId(t.n))
  }
  for (const t of opts.tabs ?? []) {
    s.tabs[tabId(t.n)] = {
      ...defaultTabState({ id: tabId(t.n), profileId: PROFILE, spaceId: SPACE, section: 'today', url: t.url, title: t.title, lastActiveAt: t.lastActiveAt ?? t.n }),
      favicon: t.favicon ?? null,
      splitId: t.splitId ?? null
    }
  }
  const w = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE })
  s.windows[WIN] = {
    ...w,
    activeTabId: opts.activeTab ? tabId(opts.activeTab) : null,
    command: { sessionId: SESSION, mode: opts.mode ?? 'new-tab', targetTabId: opts.targetTab ? tabId(opts.targetTab) : null, initialText: opts.initialText ?? '' }
  }
  return { ...s, rev: 1 }
}

export const hist = (url: string, title: string, extra: Partial<IndexRow> = {}): IndexRow => ({ key: url, kind: 'history', url, title, visitCount: 1, typedCount: 0, lastVisitAt: Date.now(), ...extra })

export interface StubAnswers { history?: IndexRow[]; suggest?: (query: string) => string[]; actions?: (query: string) => ActionRow[] }

let rev = 100

/** Installs a stub window.clu with the answers and returns its call log and a function that pushes a snapshot. */
export function installBar(answers: StubAnswers = {}): { calls: Calls; push(state: CluState): void } {
  const stub = stubClu({
    // A fresh full sync each time, so the module-level history cache never carries rows between tests.
    'command.syncIndex': () => ({ rev: ++rev, full: true, upserts: answers.history ?? [], deletes: [] }),
    'command.suggest': (p) => {
      const { requestId, query } = p as { requestId: number; query: string }
      return { requestId, items: answers.suggest?.(query) ?? [] }
    },
    'command.actions': (p) => answers.actions?.((p as { query: string }).query) ?? [],
    'command.deleteEntry': () => null,
    'command.execute': () => ({ ok: true }),
    'command.close': () => null,
    'settings.set': () => null
  })
  window.clu = { ...stub.clu, layer: 'top', slot: null }
  return { calls: stub.calls, push: (state) => act(() => stub.push({ kind: 'snapshot', rev: state.rev, state })) }
}

export const callsOf = (calls: Calls, name: string) => calls.filter((c) => c.name === name).map((c) => c.payload)
