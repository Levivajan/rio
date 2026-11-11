import { afterEach, describe, expect, it } from 'vitest'
import type { PointerEvent as ReactPointerEvent } from 'react'
import { defaultState, type CluState, type TabState } from '../../../shared/state'
import { createStateMirror, setStateMirrorForTest } from '../../common/store'
import { collectRows, pressRow, sidebarDrag } from './useSidebarDrag'

const S = 'space-home'

function at(el: Element, y: number, height: number) {
  el.getBoundingClientRect = () => ({ x: 8, y, width: 198, height, top: y, left: 8, right: 206, bottom: y + height, toJSON: () => null })
}

// With no favorites, or with Pinned empty or collapsed, a drag gets zero-height markers (nothing moves); each
// marker takes a band around itself so a drop there still lands in Favorites or Pinned.
describe('collectRows zone markers', () => {
  it('turns the zero-height markers into bands around the space header', () => {
    document.body.innerHTML = `<nav class="clu-sidebar"><div data-drop-zone="favorites"></div><div class="clu-tab-list"><div class="clu-tab-tree"><div data-drop-zone="pinned"></div></div></div></nav>`
    const root = document.querySelector('nav') as HTMLElement
    at(root.querySelector('[data-drop-zone="favorites"]') as Element, 60, 0)
    at(root.querySelector('.clu-tab-list') as Element, 96, 600)
    at(root.querySelector('.clu-tab-tree') as Element, 96, 0)
    at(root.querySelector('[data-drop-zone="pinned"]') as Element, 96, 0)
    const zones = collectRows(root, S, defaultState()).filter((r) => r.kind === 'zone')
    expect(zones.map((z) => [z.kind === 'zone' && z.zone, z.rect.y, z.rect.height])).toEqual([
      ['favorites', 52, 26],
      ['pinned', 78, 26],
      ['today-end', 96, 600]
    ])
  })

  it('drops the Pinned band once its marker scrolls out of the list', () => {
    document.body.innerHTML = `<nav class="clu-sidebar"><div class="clu-tab-list"><div class="clu-tab-tree"><div data-drop-zone="pinned"></div></div></div></nav>`
    const root = document.querySelector('nav') as HTMLElement
    at(root.querySelector('.clu-tab-list') as Element, 96, 600)
    at(root.querySelector('.clu-tab-tree') as Element, 40, 0)
    at(root.querySelector('[data-drop-zone="pinned"]') as Element, 40, 0)
    expect(collectRows(root, S, defaultState()).some((r) => r.kind === 'zone' && r.zone === 'pinned')).toBe(false)
  })
})

// Pressing a row that is not selected activates it, and Chromium then focuses the page, so the frame gets a
// window blur a few ms into the press. The press must survive it and lift the row once the pointer moves.
describe('pressRow', () => {
  afterEach(() => {
    sidebarDrag.cancel()
    setStateMirrorForTest(null)
  })

  const pointer = (type: string, x: number, y: number, buttons: number) => {
    const e = new MouseEvent(type, { clientX: x, clientY: y, buttons, bubbles: true })
    Object.defineProperty(e, 'pointerId', { value: 1 })
    window.dispatchEvent(e)
  }

  function press(): void {
    // jsdom has no pointer capture.
    const el = document.documentElement as unknown as Record<string, unknown>
    el.setPointerCapture = () => undefined
    el.hasPointerCapture = () => false
    el.releasePointerCapture = () => undefined
    const base = defaultState()
    const space = { ...(Object.values(base.spaces)[0] ?? {}), id: S, pinned: [{ kind: 'tab', id: 't1' }], today: [] } as unknown as CluState['spaces'][string]
    const state: CluState = { ...base, spaces: { [S]: space }, tabs: { t1: { id: 't1', section: 'pinned', spaceId: S, parentId: null } as unknown as TabState } }
    const mirror = createStateMirror(async () => state)
    mirror.receive({ kind: 'snapshot', rev: 1, state })
    setStateMirrorForTest(mirror)
    document.body.innerHTML = `<nav class="clu-sidebar" data-active-space="${S}"><div class="clu-tab-list"><div class="clu-tab-tree"><div role="treeitem" data-tab-id="t1">Mock Ups</div></div></div></nav>`
    const row = document.querySelector('[data-tab-id="t1"]') as HTMLElement
    at(row, 100, 34)
    pressRow({ button: 0, pointerId: 1, clientX: 40, clientY: 117, target: row, currentTarget: row } as unknown as ReactPointerEvent<HTMLElement>, 't1', ['t1'])
  }

  it('lifts the row after the window blurs when the press activates it', () => {
    press()
    window.dispatchEvent(new Event('blur'))
    pointer('pointermove', 40, 127, 1)
    expect(sidebarDrag.get().dragging).toBe(true)
    expect(sidebarDrag.get().node?.id).toBe('t1')
    pointer('pointerup', 40, 127, 0)
    expect(sidebarDrag.get().dragging).toBe(false)
  })

  it('ends the press when a move arrives with the button already up', () => {
    press()
    pointer('pointermove', 40, 127, 0)
    expect(sidebarDrag.get().dragging).toBe(false)
    pointer('pointermove', 40, 140, 1)
    expect(sidebarDrag.get().dragging).toBe(false)
  })
})
