// FakeBridge handlers of M6 (docs/chromium/m6.md): split view, the drop preview, the media card and the Peek, answered
// the way CluWindowMultitask does on Chromium. Two panes are Chromium's split, which draws their chrome, so splitView
// carries their rects with empty bars and no dividers; pane rects follow MultiContentsView's layout: flush with the
// page card, a 10 DIP resize area between them. Three or four panes sit at even shares with the same gap,
// each page under a PANE_BAR bar the frame draws (CluShell::ExtraPaneRects).
import { MAX_PANES, PANE_BAR, SPLIT_DROP_SURFACE, SPLIT_FULL_TOAST, sumsToOne, type SplitEdge } from '../../../shared/split'
import type { CluState, OverlaySurfaceState, TabState } from '../../../shared/state'
import type { Rect, SplitNode } from '../../../shared/types'

export const FAKE_MAX_PANES = MAX_PANES
export const FAKE_SPLIT_FULL_TOAST = SPLIT_FULL_TOAST
/** MultiContentsResizeArea between the two panes, and the gap between three or four. */
export const RESIZE_AREA = 10

export interface MultitaskDeps {
  state(): CluState
  set(path: string[], value: unknown): void
  del(path: string[]): void
  activate(windowId: string, tabId: string): void
  createTab(windowId: string, url: string, spaceId?: string): string
  closeTab(windowId: string, tabId: string | undefined): void
  toast(windowId: string, text: string): void
  openSurface(windowId: string, spec: Omit<OverlaySurfaceState, 'instanceId' | 'slot' | 'openedFrom' | 'layer'> & { layer?: OverlaySurfaceState['layer'] }): string
  closeSurface(windowId: string, instanceId: string): void
  openBar(windowId: string, mode: 'new-tab' | 'edit-url' | 'split-add' | 'mini'): void
  fail(code: string, reason?: string): unknown
}

type Dir = 'row' | 'col'

const uuid = () => crypto.randomUUID()

function root(dir: Dir, ids: readonly string[], ratio: number): SplitNode {
  const sizes = ids.length === 2 ? [ratio, 1 - ratio] : ids.map(() => 1 / ids.length)
  return { kind: 'group', dir, sizes, children: ids.map((tabId) => ({ kind: 'pane' as const, tabId })) }
}

function parts(node: SplitNode): { dir: Dir; ids: string[]; ratio: number } {
  if (node.kind !== 'group') return { dir: 'row', ids: [node.tabId], ratio: 0.5 }
  return { dir: node.dir, ids: node.children.flatMap((c) => (c.kind === 'pane' ? [c.tabId] : [])), ratio: node.sizes[0] ?? 0.5 }
}

/** Two pane rects inside `page`, the first taking `ratio` of the room after the resize area. */
export function paneRects(page: Rect, dir: Dir, ratio: number): [Rect, Rect] {
  const room = Math.max(0, (dir === 'row' ? page.width : page.height) - RESIZE_AREA)
  const first = Math.round(room * ratio)
  if (dir === 'row') {
    return [
      { x: page.x, y: page.y, width: first, height: page.height },
      { x: page.x + first + RESIZE_AREA, y: page.y, width: room - first, height: page.height }
    ]
  }
  return [
    { x: page.x, y: page.y, width: page.width, height: first },
    { x: page.x, y: page.y + first + RESIZE_AREA, width: page.width, height: room - first }
  ]
}

/** Three or four panes at even shares inside `page`, each page under its bar; the last pane takes the rounding. */
export function evenPanes(page: Rect, dir: Dir, count: number): { rect: Rect; bar: Rect }[] {
  const room = Math.max(0, (dir === 'row' ? page.width : page.height) - RESIZE_AREA * (count - 1))
  const each = Math.floor(room / count)
  return Array.from({ length: count }, (_, i) => {
    const size = i === count - 1 ? room - each * i : each
    const start = i * (each + RESIZE_AREA)
    const card = dir === 'row' ? { x: page.x + start, y: page.y, width: size, height: page.height } : { x: page.x, y: page.y + start, width: page.width, height: size }
    const bar = Math.min(PANE_BAR, card.height)
    return { bar: { ...card, height: bar }, rect: { ...card, y: card.y + bar, height: Math.max(1, card.height - bar) } }
  })
}

/** Half of a pane toward `edge`, the drop preview. */
export function halfToward(pane: Rect, edge: SplitEdge): Rect {
  const w = Math.floor(pane.width / 2)
  const h = Math.floor(pane.height / 2)
  if (edge === 'left') return { ...pane, width: w }
  if (edge === 'right') return { ...pane, x: pane.x + pane.width - w, width: w }
  if (edge === 'top') return { ...pane, height: h }
  return { ...pane, y: pane.y + pane.height - h, height: h }
}

export function createFakeMultitask(d: MultitaskDeps) {
  const win = (id: string) => d.state().windows[id]
  const tabOf = (id: string): TabState | undefined => d.state().tabs[id]
  const splitOf = (tabId: string) => {
    const splitId = tabOf(tabId)?.splitId
    return splitId ? d.state().splits[splitId] : undefined
  }
  let dropInstance: string | null = null
  // A tab the card paused keeps its card until it is seen (CluMediaCards).
  let held: string | null = null

  /** windows[id].mediaTabId: a tab that plays heard media out of sight, or the one the card paused. */
  function refreshMedia(windowId: string, shown: readonly string[]): void {
    if (held && shown.includes(held)) held = null
    const tabs = Object.values(d.state().tabs)
    const playing = tabs.find((t) => !shown.includes(t.id) && t.mediaPlaying && (t.audible || t.muted))
    const card = held && tabOf(held) ? held : (playing?.id ?? null)
    if (win(windowId)?.mediaTabId !== card) d.set(['windows', windowId, 'mediaTabId'], card)
  }

  /** windows[id].splitView and activeSplitId follow the active tab. */
  function refresh(windowId: string): void {
    const w = win(windowId)
    if (!w) return
    const split = w.activeTabId ? splitOf(w.activeTabId) : undefined
    refreshMedia(windowId, split ? parts(split.root).ids : w.activeTabId ? [w.activeTabId] : [])
    if (!split) {
      d.set(['windows', windowId, 'activeSplitId'], null)
      d.set(['windows', windowId, 'splitView'], null)
      return
    }
    const { dir, ids, ratio } = parts(split.root)
    const page = w.shell.layout.pageRect
    const rects = ids.length > 2 ? evenPanes(page, dir, ids.length) : paneRects(page, dir, ratio).map((rect) => ({ rect, bar: { x: rect.x, y: rect.y, width: rect.width, height: 0 } }))
    d.set(['windows', windowId, 'activeSplitId'], split.id)
    d.set(['windows', windowId, 'splitView'], {
      splitId: split.id,
      panes: ids.map((tabId, i) => ({ tabId, ...(rects[i] as { rect: Rect; bar: Rect }) })),
      dividers: []
    })
  }

  function dissolve(splitId: string): void {
    const split = d.state().splits[splitId]
    if (!split) return
    for (const id of parts(split.root).ids) if (tabOf(id)?.splitId === splitId) d.set(['tabs', id, 'splitId'], null)
    d.del(['splits', splitId])
  }

  /** CluModelService::CreateSplit: 2 to 4 tab rows of one space, never favorites; the rest join the first's list. */
  function make(windowId: string, ids: string[], dir: Dir, focused: string): unknown {
    if (ids.length > FAKE_MAX_PANES) {
      d.toast(windowId, FAKE_SPLIT_FULL_TOAST)
      return d.fail('rejected', 'split-full')
    }
    const tabs = ids.map(tabOf)
    if (tabs.some((t) => !t) || new Set(ids).size !== ids.length) return d.fail('not-found')
    if (tabs.some((t) => t?.section === 'favorite')) return d.fail('bad-payload')
    const spaceId = tabs[0]?.spaceId ?? ''
    if (tabs.some((t) => t?.spaceId !== spaceId)) return d.fail('bad-payload')
    for (const id of ids) {
      const old = tabOf(id)?.splitId
      if (old) dissolve(old)
    }
    // The other panes sit right after the first in a Today list (the sidebar shows the split in the first's slot).
    const space = d.state().spaces[spaceId]
    if (space && ids.every((id) => space.today.includes(id))) {
      const after = ids.slice(1)
      const rest = space.today.filter((id) => !after.includes(id))
      const at = rest.indexOf(ids[0] as string)
      d.set(['spaces', spaceId, 'today'], [...rest.slice(0, at + 1), ...after, ...rest.slice(at + 1)])
    }
    const splitId = uuid()
    d.set(['splits', splitId], { id: splitId, spaceId, root: root(dir, ids, 0.5), focusedTabId: focused })
    for (const id of ids) d.set(['tabs', id, 'splitId'], splitId)
    d.activate(windowId, focused)
    refresh(windowId)
    return { splitId }
  }

  function arrange(windowId: string, splitId: string, ids: string[], dir: Dir): void {
    const split = d.state().splits[splitId]
    if (!split) return
    const before = parts(split.root)
    const ratio = before.ids[0] === ids[0] ? before.ratio : 1 - before.ratio
    d.set(['splits', splitId, 'root'], root(dir, ids, ratio))
    refresh(windowId)
  }

  function focusTab(windowId: string, tabId: string | undefined): void {
    if (!tabId) return
    d.activate(windowId, tabId)
    const split = splitOf(tabId)
    if (split) d.set(['splits', split.id, 'focusedTabId'], tabId)
    refresh(windowId)
  }

  function shownSplit(windowId: string) {
    const active = win(windowId)?.activeTabId
    return active ? splitOf(active) : undefined
  }

  function removePane(windowId: string, tabId: string, close: boolean): unknown {
    const split = splitOf(tabId)
    if (!split) return d.fail('not-found')
    const { dir, ids } = parts(split.root)
    if (ids.length > 2) {
      // The other panes stay a split; the focus moves to the next pane, or the one before at the end.
      const at = ids.indexOf(tabId)
      const shown = win(windowId)?.activeTabId
      const focus = shown && shown !== tabId ? shown : ((at + 1 < ids.length ? ids[at + 1] : ids[at - 1]) as string)
      make(windowId, ids.filter((id) => id !== tabId), dir, focus)
      if (close) d.closeTab(windowId, tabId)
      refresh(windowId)
      return null
    }
    const other = ids.find((id) => id !== tabId)
    const shown = win(windowId)?.activeTabId
    dissolve(split.id)
    if (other && (shown === tabId || shown === other)) d.activate(windowId, other)
    if (close) d.closeTab(windowId, tabId)
    refresh(windowId)
    return null
  }

  const handlers = {
    'split.create': (p: { tabIds: string[]; dir: Dir }, { windowId }: { windowId: string }) => make(windowId, p.tabIds, p.dir, p.tabIds[0] as string),
    'split.dropTab': (p: { tabId: string; targetTabId: string; edge: SplitEdge }, { windowId }: { windowId: string }) => {
      const dir: Dir = p.edge === 'left' || p.edge === 'right' ? 'row' : 'col'
      const order = p.edge === 'left' || p.edge === 'top' ? [p.tabId, p.targetTabId] : [p.targetTabId, p.tabId]
      const into = splitOf(p.targetTabId)
      if (into) {
        const ids = parts(into.root).ids
        if (!ids.includes(p.tabId)) {
          // A third or fourth pane joins beside the target in the split's direction.
          if (ids.length >= FAKE_MAX_PANES) {
            d.toast(windowId, FAKE_SPLIT_FULL_TOAST)
            return d.fail('rejected', 'split-full')
          }
          const at = ids.indexOf(p.targetTabId) + (p.edge === 'left' || p.edge === 'top' ? 0 : 1)
          return make(windowId, [...ids.slice(0, at), p.tabId, ...ids.slice(at)], parts(into.root).dir, p.tabId)
        }
        if (ids.length > 2) {
          // A move inside a split of 3 or 4: the pane goes beside the target.
          const rest = ids.filter((id) => id !== p.tabId)
          const at = rest.indexOf(p.targetTabId) + (p.edge === 'left' || p.edge === 'top' ? 0 : 1)
          arrange(windowId, into.id, [...rest.slice(0, at), p.tabId, ...rest.slice(at)], parts(into.root).dir)
        } else arrange(windowId, into.id, order, dir)
        focusTab(windowId, p.tabId)
        return { splitId: into.id }
      }
      return make(windowId, order, dir, p.tabId)
    },
    'split.swap': (p: { tabId: string; targetTabId: string }, { windowId }: { windowId: string }) => {
      const split = splitOf(p.tabId)
      if (!split || !parts(split.root).ids.includes(p.targetTabId)) return d.fail('not-found')
      const { dir, ids } = parts(split.root)
      const order = ids.map((id) => (id === p.tabId ? p.targetTabId : id === p.targetTabId ? p.tabId : id))
      arrange(windowId, split.id, order, dir)
      return null
    },
    'split.resize': (p: { splitId: string; path: number[]; sizes: number[] }, { windowId }: { windowId: string }) => {
      const split = d.state().splits[p.splitId]
      if (!split) return d.fail('not-found')
      if (p.path.length > 0 || p.sizes.length !== 2 || !sumsToOne(p.sizes)) return d.fail('stale')
      const { dir, ids } = parts(split.root)
      d.set(['splits', p.splitId, 'root'], root(dir, ids, Math.min(0.95, Math.max(0.05, p.sizes[0] as number))))
      refresh(windowId)
      return null
    },
    'split.equalize': (p: { splitId: string }, { windowId }: { windowId: string }) => {
      const split = d.state().splits[p.splitId]
      if (!split) return d.fail('not-found')
      const { dir, ids } = parts(split.root)
      d.set(['splits', p.splitId, 'root'], root(dir, ids, 0.5))
      refresh(windowId)
      return null
    },
    'split.removePane': (p: { tabId: string; mode: 'separate' | 'close' }, { windowId }: { windowId: string }) => removePane(windowId, p.tabId, p.mode === 'close'),
    'split.focusPane': (p: { tabId?: string; index?: number; step?: 1 | -1 }, { windowId }: { windowId: string }) => {
      const split = shownSplit(windowId)
      if (!split) return d.fail('not-found')
      const ids = parts(split.root).ids
      const current = win(windowId)?.activeTabId ?? ''
      const target =
        p.tabId !== undefined ? (ids.includes(p.tabId) ? p.tabId : undefined)
        : p.index !== undefined ? ids[p.index - 1]
        : ids[(ids.indexOf(current) + (p.step ?? 1) + ids.length) % ids.length]
      if (!target) return d.fail('not-found')
      focusTab(windowId, target)
      return null
    },
    'split.dragState': (p: { phase: 'start' | 'hover' | 'end' | 'cancel'; tabId: string; targetTabId?: string; edge?: SplitEdge }, { windowId }: { windowId: string }) => {
      const w = win(windowId)
      if (!w) return null
      if (p.phase === 'start') {
        d.set(['windows', windowId, 'splitDrop'], { tabId: p.tabId, targetTabId: null, edge: null, zone: null })
        dropInstance = d.openSurface(windowId, { surface: SPLIT_DROP_SURFACE, modality: 'passive', focus: false, rect: null, props: {} })
        return null
      }
      if (w.splitDrop?.tabId !== p.tabId) return null
      if (p.phase === 'hover') {
        const pane = p.targetTabId ? (w.splitView?.panes.find((x) => x.tabId === p.targetTabId)?.rect ?? (p.targetTabId === w.activeTabId ? w.shell.layout.pageRect : null)) : null
        d.set(['windows', windowId, 'splitDrop'], {
          tabId: p.tabId,
          targetTabId: pane ? (p.targetTabId ?? null) : null,
          edge: pane && p.edge ? p.edge : null,
          zone: pane ? (p.edge ? halfToward(pane, p.edge) : pane) : null
        })
        return null
      }
      d.set(['windows', windowId, 'splitDrop'], null)
      if (dropInstance) d.closeSurface(windowId, dropInstance)
      dropInstance = null
      return null
    },
    'media.toggle': (p: { tabId: string }) => {
      const tab = tabOf(p.tabId)
      if (!tab || (!tab.mediaPlaying && held !== p.tabId)) return d.fail('not-found')
      if (tab.mediaPlaying) held = p.tabId
      d.set(['tabs', p.tabId, 'mediaPlaying'], !tab.mediaPlaying)
      return null
    },
    // The fake has no media session; the browser hands these to the page's (content::MediaSession).
    'media.previous': (p: { tabId: string }) => (tabOf(p.tabId) ? null : d.fail('not-found')),
    'media.next': (p: { tabId: string }) => (tabOf(p.tabId) ? null : d.fail('not-found')),
    'media.focus': (p: { tabId: string }, { windowId }: { windowId: string }) => {
      if (!tabOf(p.tabId)) return d.fail('not-found')
      d.activate(windowId, p.tabId)
      return null
    },
    'peek.close': (_p: unknown, { windowId }: { windowId: string }) => {
      d.set(['windows', windowId, 'peek'], null)
      return null
    },
    'peek.expand': (_p: unknown, { windowId }: { windowId: string }) => {
      const peek = win(windowId)?.peek
      if (!peek) return d.fail('not-found')
      const id = d.createTab(windowId, peek.url)
      d.activate(windowId, id)
      d.set(['windows', windowId, 'peek'], null)
      return null
    },
    'peek.nav': (_p: unknown, { windowId }: { windowId: string }) => (win(windowId)?.peek ? null : d.fail('not-found')),
    'peek.animationDone': () => null
  }

  /** Table commands of M6, as CluWindowMultitask::Dispatch runs them; false when not one of them. */
  function runCommand(windowId: string, id: string): boolean {
    if (id === 'split.add' || id === 'split.addToSplit' || id === 'split.openInSplit') {
      const split = shownSplit(windowId)
      if (split && parts(split.root).ids.length >= FAKE_MAX_PANES) d.toast(windowId, FAKE_SPLIT_FULL_TOAST)
      else d.openBar(windowId, 'split-add')
      return true
    }
    const split = shownSplit(windowId)
    if (!split) return id.startsWith('split.')
    const ids = parts(split.root).ids
    const current = win(windowId)?.activeTabId ?? ''
    if (id === 'split.separate') removePane(windowId, current, false)
    else if (id === 'split.next' || id === 'split.prev') focusTab(windowId, ids[(ids.indexOf(current) + (id === 'split.next' ? 1 : -1) + ids.length) % ids.length])
    else if (/^split\.focus[1-4]$/.test(id)) focusTab(windowId, ids[Number(id.slice(-1)) - 1])
    else return false
    return true
  }

  return { handlers, runCommand, refresh }
}
