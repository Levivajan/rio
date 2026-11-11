import { describe, expect, it } from 'vitest'
import { createFakeHost } from './fake'
import { PANE_BAR } from '../../../shared/split'
import { evenPanes, FAKE_SPLIT_FULL_TOAST, halfToward, paneRects, RESIZE_AREA } from './fake-multitask'
import { FAKE } from './fake-seed'

// The FakeBridge answers split, media and Peek commands as CluWindowMultitask does on Chromium (docs/chromium/m6.md):
// two panes with Chromium's own pane chrome, three or four with the frame's bars, the drop preview, the media card.
const a = FAKE.tab(20)
const b = FAKE.tab(21)
const c = FAKE.tab(23)
const d4 = FAKE.tab(24)
const e5 = FAKE.tab(22)

function setup() {
  const host = createFakeHost()
  host.setContentSize({ width: 1280, height: 800 })
  const ui = host.connect({ surface: 'ui' })
  const w = () => host.state().windows[FAKE.window]
  return { host, ui, w }
}

describe('pane geometry', () => {
  it('lays two panes out flush with the page, the resize area between them', () => {
    const [left, right] = paneRects({ x: 260, y: 8, width: 1010, height: 784 }, 'row', 0.5)
    expect(left).toEqual({ x: 260, y: 8, width: 500, height: 784 })
    expect(right).toEqual({ x: 260 + 500 + RESIZE_AREA, y: 8, width: 500, height: 784 })
    const [top, bottom] = paneRects({ x: 0, y: 0, width: 800, height: 610 }, 'col', 0.25)
    expect(top.height).toBe(150)
    expect(bottom.y).toBe(160)
  })

  it('lays three or four panes out at even shares, each page under its bar', () => {
    const panes = evenPanes({ x: 260, y: 8, width: 1010, height: 784 }, 'row', 3)
    expect(panes.map((p) => p.bar.width)).toEqual([330, 330, 330])
    expect(panes[1]?.bar.x).toBe(260 + 330 + RESIZE_AREA)
    expect(panes[0]?.rect).toEqual({ x: 260, y: 8 + PANE_BAR, width: 330, height: 784 - PANE_BAR })
    const stacked = evenPanes({ x: 0, y: 0, width: 800, height: 630 }, 'col', 4)
    expect(stacked.map((p) => p.bar.y)).toEqual([0, 160, 320, 480])
  })

  it('previews half of the target pane toward the edge', () => {
    const pane = { x: 100, y: 10, width: 400, height: 300 }
    expect(halfToward(pane, 'right')).toEqual({ x: 300, y: 10, width: 200, height: 300 })
    expect(halfToward(pane, 'bottom')).toEqual({ x: 100, y: 160, width: 400, height: 150 })
  })
})

describe('FakeBridge split view', () => {
  it('a drop on the right edge makes a split row with Chromium pane chrome; a third and fourth pane join, a fifth is refused', async () => {
    const { host, ui, w } = setup()
    await ui.tabs.activate({ tabId: a })
    expect(await ui.split.dropTab({ tabId: b, targetTabId: a, edge: 'right' })).toMatchObject({ ok: true })
    host.flush()
    const splitId = w()?.activeSplitId ?? ''
    const split = host.state().splits[splitId]
    expect(split?.root).toMatchObject({ kind: 'group', dir: 'row', children: [{ tabId: a }, { tabId: b }] })
    expect(w()?.activeTabId).toBe(b)
    expect(w()?.splitView?.panes.map((p) => [p.tabId, p.bar.height])).toEqual([[a, 0], [b, 0]])
    expect(w()?.splitView?.dividers).toEqual([])
    // The second pane joins the first's slot in Today.
    const today = host.state().spaces[host.state().tabs[a]?.spaceId ?? '']?.today ?? []
    expect(today.indexOf(b)).toBe(today.indexOf(a) + 1)

    // A third pane joins beside its target instead of replacing a pane, with the frame's bars.
    expect(await ui.split.dropTab({ tabId: c, targetTabId: a, edge: 'right' })).toMatchObject({ ok: true })
    host.flush()
    const three = host.state().splits[w()?.activeSplitId ?? '']
    expect(three?.root).toMatchObject({ kind: 'group', dir: 'row', children: [{ tabId: a }, { tabId: c }, { tabId: b }] })
    expect(w()?.splitView?.panes.map((p) => [p.tabId, p.bar.height])).toEqual([[a, PANE_BAR], [c, PANE_BAR], [b, PANE_BAR]])
    expect(await ui.split.dropTab({ tabId: d4, targetTabId: b, edge: 'right' })).toMatchObject({ ok: true })
    host.flush()
    expect(w()?.splitView?.panes.map((p) => p.tabId)).toEqual([a, c, b, d4])

    expect(await ui.split.dropTab({ tabId: e5, targetTabId: a, edge: 'left' })).toMatchObject({ ok: false, error: { code: 'rejected', reason: 'split-full' } })
    expect(w()?.toasts.at(-1)?.text).toBe(FAKE_SPLIT_FULL_TOAST)
    expect(FAKE_SPLIT_FULL_TOAST).toBe('A split holds up to 4 tabs')
    expect(host.state().tabs[e5]?.splitId).toBeNull()

    // Closing a pane keeps the other three a split.
    expect(await ui.split.removePane({ tabId: c, mode: 'close' })).toMatchObject({ ok: true })
    host.flush()
    expect(w()?.splitView?.panes.map((p) => p.tabId)).toEqual([a, b, d4])
    expect(host.state().tabs[c]).toBeUndefined()
  })

  it('swaps, resizes, steps the focus with wrap and separates the focused pane', async () => {
    const { host, ui, w } = setup()
    const made = await ui.split.create({ tabIds: [a, b], dir: 'row' })
    expect(made.ok).toBe(true)
    const splitId = made.ok ? made.value.splitId : ''
    await ui.split.swap({ tabId: a, targetTabId: b })
    expect(host.state().splits[splitId]?.root).toMatchObject({ children: [{ tabId: b }, { tabId: a }] })
    await ui.split.resize({ splitId, path: [], sizes: [0.3, 0.7] })
    expect(host.state().splits[splitId]?.root).toMatchObject({ sizes: [0.3, 0.7] })
    expect(await ui.split.resize({ splitId, path: [0], sizes: [0.3, 0.7] })).toMatchObject({ ok: false, error: { code: 'stale' } })
    await ui.split.equalize({ splitId, path: [] })
    expect(host.state().splits[splitId]?.root).toMatchObject({ sizes: [0.5, 0.5] })

    expect(w()?.activeTabId).toBe(a)
    await ui.commands.run({ id: 'split.next' })
    expect(w()?.activeTabId).toBe(b)
    await ui.commands.run({ id: 'split.next' })
    expect(w()?.activeTabId).toBe(a)
    await ui.split.focusPane({ index: 1 })
    expect(w()?.activeTabId).toBe(b)
    await ui.commands.run({ id: 'split.separate' })
    expect(host.state().splits[splitId]).toBeUndefined()
    expect(host.state().tabs[a]?.splitId).toBeNull()
    expect(w()?.activeTabId).toBe(a)
    expect(w()?.splitView).toBeNull()
  })

  it('the drag preview follows the hovered edge and goes with the drag', async () => {
    const { ui, w } = setup()
    await ui.tabs.activate({ tabId: a })
    await ui.split.dragState({ phase: 'start', tabId: b })
    expect(w()?.overlay.surfaces.map((s) => [s.surface, s.layer, s.focus])).toEqual([['split-drop', 'top', false]])
    await ui.split.dragState({ phase: 'hover', tabId: b, targetTabId: a, edge: 'right' })
    const page = w()?.shell.layout.pageRect
    expect(w()?.splitDrop).toMatchObject({ targetTabId: a, edge: 'right', zone: { width: Math.floor((page?.width ?? 0) / 2) } })
    await ui.split.dragState({ phase: 'hover', tabId: b, targetTabId: c, edge: 'left' })
    expect(w()?.splitDrop).toMatchObject({ targetTabId: null, zone: null })
    await ui.split.dragState({ phase: 'cancel', tabId: b })
    expect(w()?.splitDrop).toBeNull()
    expect(w()?.overlay.surfaces).toEqual([])
  })

  it('Add Split View opens the bar in split-add mode, or says the split is full', async () => {
    const { ui, w } = setup()
    await ui.tabs.activate({ tabId: a })
    await ui.commands.run({ id: 'split.add' })
    expect(w()?.command?.mode).toBe('split-add')
    await ui.split.create({ tabIds: [a, b], dir: 'row' })
    const toasts = w()?.toasts.length
    // Two panes have room for more: no toast.
    await ui.commands.run({ id: 'split.add' })
    expect(w()?.toasts.length).toBe(toasts)
    await ui.split.create({ tabIds: [a, b, c, d4], dir: 'row' })
    await ui.commands.run({ id: 'split.add' })
    expect(w()?.toasts.at(-1)?.text).toBe(FAKE_SPLIT_FULL_TOAST)
  })
})

describe('FakeBridge Peek', () => {
  it('opening the command bar closes an open Peek', async () => {
    const { host, ui, w } = setup()
    host.edit(['windows', FAKE.window, 'peek'], { seq: 1, url: 'https://peek.example.com/', host: 'peek.example.com', title: 'Peek', favicon: null, canGoBack: false, originRect: { x: 10, y: 10, width: 80, height: 16 }, cardRect: { x: 0, y: 0, width: 600, height: 500 }, summary: null })
    expect(w()?.peek).not.toBeNull()
    await ui.command.open({ mode: 'new-tab' })
    expect(w()?.command?.mode).toBe('new-tab')
    expect(w()?.peek).toBeNull()
  })
})

describe('FakeBridge media card', () => {
  it('shows a heard background tab, keeps it while the card paused it, and lets go once it is seen', async () => {
    const { host, ui, w } = setup()
    const card = w()?.mediaTabId ?? ''
    expect(host.state().tabs[card]?.mediaPlaying).toBe(true)
    await ui.media.toggle({ tabId: card })
    expect(host.state().tabs[card]?.mediaPlaying).toBe(false)
    await ui.tabs.activate({ tabId: a })
    expect(w()?.mediaTabId).toBe(card)
    await ui.media.toggle({ tabId: card })
    expect(host.state().tabs[card]?.mediaPlaying).toBe(true)
    await ui.media.focus({ tabId: card })
    expect(w()?.activeTabId).toBe(card)
    expect(w()?.mediaTabId).not.toBe(card)
    expect(await ui.media.toggle({ tabId: a })).toMatchObject({ ok: false, error: { code: 'not-found' } })
  })
})
