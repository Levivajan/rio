import { afterEach, describe, expect, it, vi } from 'vitest'
import { dragBus, type DragEvent, type DragPhase } from '../../common/dragBus'
import type { DraggedNode, FlatRow } from './computeDrop'
import { bulkDropIndex, createSidebarDrag, spaceIconTarget, SPRING_LOAD_MS, type DragEnv } from './sidebarDrag'

const S = 'space-home'
const OTHER = 'space-other'
const SIDEBAR = { x: 0, y: 0, width: 260, height: 800 }
const row = (id: string, index: number, y: number): FlatRow => ({ kind: 'tab', id, section: 'today', spaceId: S, level: 1, path: [{ id, parentId: null, index }], rect: { x: 0, y, width: 260, height: 34 } })
const ROWS = [row('t0', 0, 100), row('t1', 1, 134), row('t2', 2, 168)]
const R0 = { x: 0, y: 100, width: 260, height: 34 }
const ICON = { x: 200, y: 770, width: 24, height: 24 }
const t0: DraggedNode = { id: 't0', kind: 'tab', section: 'today', spaceId: S, parentId: null, index: 0 }

function harness() {
  let active = S
  const events: DragEvent[] = []
  const offs = (['start', 'move', 'outside', 'end', 'cancel'] as DragPhase[]).map((p) => dragBus.on(p, (e) => events.push(e)))
  const env: DragEnv = {
    rows: () => ROWS,
    sidebarRect: () => SIDEBAR,
    spaceAt: (x, y) => (x >= ICON.x && x < ICON.x + ICON.width && y >= ICON.y && y < ICON.y + ICON.height ? OTHER : null),
    activeSpaceId: () => active,
    pinnedCount: () => 4,
    move: vi.fn(),
    moveTabs: vi.fn(),
    switchSpace: vi.fn((id: string) => void (active = id)),
    expandFolder: vi.fn(),
    emit: (e) => dragBus.emit(e),
    setTimer: (fn, ms) => setTimeout(fn, ms),
    clearTimer: (h) => clearTimeout(h as ReturnType<typeof setTimeout>)
  }
  const drag = createSidebarDrag(env)
  return { drag, env, events, phases: () => events.map((e) => e.phase), off: () => offs.forEach((o) => o()) }
}

let h: ReturnType<typeof harness> | null = null
afterEach(() => {
  h?.off()
  h = null
  vi.useRealTimers()
})

describe('sidebar drag', () => {
  it('a 3 px move stays a press; 4 px starts the drag with start then move', () => {
    h = harness()
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(53, 110)
    expect(h.drag.get().dragging).toBe(false)
    expect(h.drag.release(53, 110)).toBe(false)
    expect(h.events).toEqual([])
    expect(h.env.move).not.toHaveBeenCalled()

    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(50, 114)
    expect(h.drag.get()).toMatchObject({ dragging: true, grabX: 50, grabY: 10 })
    expect(h.phases()).toEqual(['start', 'move'])
    expect(h.events[0]).toMatchObject({ tabId: 't0', tabIds: ['t0'], x: 50, y: 114 })
  })

  it('leaving the sidebar emits outside with the tab id and pointer position and shows no drop; a release there moves nothing', () => {
    h = harness()
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(50, 160)
    expect(h.drag.get().drop?.target).toEqual({ section: 'today', spaceId: S, index: 1 })
    h.drag.move(400, 300)
    h.drag.move(640, 320)
    expect(h.events.filter((e) => e.phase === 'outside')).toEqual([
      { phase: 'outside', x: 400, y: 300, tabId: 't0', tabIds: ['t0'] },
      { phase: 'outside', x: 640, y: 320, tabId: 't0', tabIds: ['t0'] }
    ])
    expect(h.drag.get().drop).toBeNull()
    expect(h.drag.release(640, 320)).toBe(true)
    expect(h.env.move).not.toHaveBeenCalled()
    expect(h.phases().at(-1)).toBe('end')
    expect(h.events.at(-1)).toMatchObject({ x: 640, y: 320, tabId: 't0' })
  })

  it('Esc (cancel) emits cancel, moves nothing and ends the drag', () => {
    h = harness()
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(50, 190)
    h.drag.cancel()
    expect(h.phases()).toEqual(['start', 'move', 'cancel'])
    expect(h.events.at(-1)).toMatchObject({ phase: 'cancel', tabId: 't0', x: 50, y: 190 })
    expect(h.drag.get().dragging).toBe(false)
    expect(h.drag.release(50, 190)).toBe(false)
    expect(h.env.move).not.toHaveBeenCalled()
  })

  it('a release inside the sidebar moves to the computed target', () => {
    h = harness()
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(50, 190)
    h.drag.release(50, 195)
    expect(h.env.move).toHaveBeenCalledWith('t0', { section: 'today', spaceId: S, index: 2 })
  })

  it('resting 500 ms on another space icon switches the sidebar; leaving earlier does not', () => {
    vi.useFakeTimers()
    h = harness()
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(205, 775)
    expect(h.drag.get().spaceHover).toBe(OTHER)
    vi.advanceTimersByTime(SPRING_LOAD_MS - 50)
    h.drag.move(50, 150)
    vi.advanceTimersByTime(SPRING_LOAD_MS)
    expect(h.env.switchSpace).not.toHaveBeenCalled()
    h.drag.move(206, 776)
    h.drag.move(207, 777)
    vi.advanceTimersByTime(SPRING_LOAD_MS)
    expect(h.env.switchSpace).toHaveBeenCalledWith(OTHER)
    expect(h.env.switchSpace).toHaveBeenCalledTimes(1)
  })

  it('a release on a space icon puts a Today row at the top of Today and anchored rows at the end of pinned', () => {
    h = harness()
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(205, 775)
    h.drag.release(205, 775)
    expect(h.env.move).toHaveBeenCalledWith('t0', { section: 'today', spaceId: OTHER, index: 0 })
    expect(spaceIconTarget({ ...t0, section: 'pinned' }, OTHER, 4)).toEqual({ section: 'pinned', spaceId: OTHER, parentId: null, index: 4 })
    expect(spaceIconTarget({ ...t0, section: 'favorite', spaceId: null }, OTHER, 0)).toEqual({ section: 'pinned', spaceId: OTHER, parentId: null, index: 0 })
    expect(spaceIconTarget({ ...t0, kind: 'folder', section: 'pinned' }, OTHER, 2)).toEqual({ section: 'pinned', spaceId: OTHER, parentId: null, index: 2 })
  })

  it('resting 500 ms in the middle of a collapsed folder expands it', () => {
    vi.useFakeTimers()
    h = harness()
    const folder: FlatRow = { kind: 'folder', id: 'F', section: 'pinned', spaceId: S, level: 1, path: [{ id: 'F', parentId: null, index: 0 }], rect: { x: 0, y: 40, width: 260, height: 34 }, expanded: false, childCount: 2 }
    h.env.rows = () => [folder, ...ROWS]
    h.drag.press(t0, ['t0'], 50, 110, R0)
    h.drag.move(50, 57)
    expect(h.drag.get().drop?.target).toEqual({ section: 'pinned', spaceId: S, parentId: 'F', index: 2 })
    vi.advanceTimersByTime(SPRING_LOAD_MS)
    expect(h.env.expandFolder).toHaveBeenCalledWith('F')
  })

  it('a press on a selected row carries every selected id on the bus and drops them together', () => {
    h = harness()
    h.drag.press(t0, ['t0', 't2'], 50, 110, R0)
    h.drag.move(50, 190)
    expect(h.events[0]).toMatchObject({ phase: 'start', tabId: 't0', tabIds: ['t0', 't2'] })
    h.drag.release(50, 195)
    expect(h.env.moveTabs).toHaveBeenCalledWith(['t0', 't2'], 't0', { section: 'today', spaceId: S, index: 2 })
    expect(h.env.move).not.toHaveBeenCalled()
  })
})

describe('bulkDropIndex', () => {
  const L = ['a', 'b', 'c', 'd', 'e']
  it('keeps the row the pressed one would land before, counted without every moved tab', () => {
    // b pressed, b and d selected; the slot before e (index 3 without b) is index 2 without b and d.
    expect(bulkDropIndex(L, 'b', ['b', 'd'], 3)).toBe(2)
    // The slot before d skips d, which moves too, and lands before e.
    expect(bulkDropIndex(L, 'b', ['b', 'd'], 2)).toBe(2)
    expect(bulkDropIndex(L, 'b', ['b', 'd'], 0)).toBe(0)
    expect(bulkDropIndex(L, 'b', ['b', 'd'], 4)).toBe(3)
  })
  it('counts another list as it is, and an index past the end appends', () => {
    expect(bulkDropIndex(['x', 'y'], 'b', ['b', 'd'], 1)).toBe(1)
    expect(bulkDropIndex([], 'b', ['b', 'd'], 0)).toBe(0)
    expect(bulkDropIndex(['x'], 'b', ['b'], 9)).toBe(1)
  })
})
