// Space theme editor (docs/chromium/parity-theme.md): the layout, wheel drags
// preview live and commit on release, each handle moves on its own, the wavy slider and the wrapping grain dial,
// sliding preset pages, Esc's close and the place beside the sidebar.
import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { defaultSpaceState, defaultState, defaultWindowState, type OverlaySurfaceState } from '../../../shared/state'
import { CANVAS, PICKER_GAP, SLIDER, SWATCHES, SWATCH_PAGES, SWATCH_SLIDE_EASING, canvasToColor, dialValue, harmony, sliderValue, swatchColor } from '../../../shared/theme/picker'
import type { SpaceTheme } from '../../../shared/theme/types'
import { stubClu } from '../../../test-utils/stubClu'
import { CLOSE_MS, ThemePicker } from './ThemePicker'

const SPACE = 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa'
const WIN = 'w'

const proto = HTMLElement.prototype as unknown as Record<string, unknown>
const saved = { set: proto.setPointerCapture, has: proto.hasPointerCapture, rel: proto.releasePointerCapture, rect: proto.getBoundingClientRect }
beforeEach(() => {
  const captured = new WeakSet<Element>()
  proto.setPointerCapture = function (this: Element) { captured.add(this) }
  proto.hasPointerCapture = function (this: Element) { return captured.has(this) }
  proto.releasePointerCapture = function (this: Element) { captured.delete(this) }
  // jsdom has no layout: each part reports the rect it has in the 356 x 508 popover placed at (0, 0).
  proto.getBoundingClientRect = function (this: HTMLElement) {
    const at = (x: number, y: number, w: number, h: number) => ({ left: x, top: y, width: w, height: h, x, y, right: x + w, bottom: y + h, toJSON: () => ({}) })
    if (this.classList.contains('clu-tp-canvas')) return at(CANVAS.x, CANVAS.y, CANVAS.size, CANVAS.size)
    if (this.classList.contains('clu-tp-slider')) return at(SLIDER.x, SLIDER.centerY - 8, SLIDER.width, SLIDER.height)
    if (this.classList.contains('clu-tp-dial')) return at(298.5 - 41, 452.5 - 41, 82, 82)
    return at(0, 0, 0, 0)
  }
})
afterEach(() => {
  proto.setPointerCapture = saved.set
  proto.hasPointerCapture = saved.has
  proto.releasePointerCapture = saved.rel
  proto.getBoundingClientRect = saved.rect
  vi.useRealTimers()
})

function setup(theme: SpaceTheme = { colors: [{ h: 0, s: 0.5 }], intensity: 0.5, grain: 0.3 }, opts: { sidebarWidth?: number; hidden?: boolean } = {}) {
  const stub = stubClu()
  window.clu = stub.clu
  const state = defaultState()
  state.rev = 1
  state.spaces[SPACE] = { ...defaultSpaceState({ id: SPACE, name: 'Work', icon: 'circle' as never, color: 'pink', profileId: 'p', ephemeral: false }), theme }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE })
  const width = opts.sidebarWidth ?? 240
  win.shell.sidebar.hidden = !!opts.hidden
  win.shell.layout.sidebarRect = { x: 0, y: 0, width, height: 800 }
  state.windows[WIN] = win
  const instance: OverlaySurfaceState = { instanceId: 'i1', surface: 'theme-picker', layer: 'top', modality: 'modal', rect: null, openedFrom: null, focus: true, slot: null, props: { spaceId: SPACE, anchor: { x: 12, y: 60, width: 200, height: 28 } } }
  render(<ThemePicker instance={instance} windowId={WIN} />)
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  const calls = (name: string) => stub.calls.filter((c) => c.name === name).map((c) => c.payload)
  const last = (name: string) => calls(name).at(-1) as { theme: SpaceTheme } | undefined
  /** A later snapshot from the browser: an echo of the editor's own write or a change made elsewhere. */
  const echo = (edit: (s: typeof state) => void) => {
    edit(state)
    state.rev += 1
    act(() => stub.push({ kind: 'snapshot', rev: state.rev, state: structuredClone(state) }))
  }
  return { calls, last, echo }
}

const dialog = () => screen.getByRole('dialog', { name: 'Theme for Work' })
const canvas = () => screen.getByTestId('theme-field')
const handles = () => dialog().querySelectorAll<HTMLElement>('.clu-tp-handle')
const drag = (el: Element, points: [number, number][]) => {
  const [first, ...rest] = points
  fireEvent.pointerDown(el, { button: 0, pointerId: 1, clientX: first?.[0], clientY: first?.[1] })
  for (const [x, y] of rest) fireEvent.pointerMove(el, { pointerId: 1, clientX: x, clientY: y })
  const end = points.at(-1) as [number, number]
  fireEvent.pointerUp(el, { pointerId: 1, clientX: end[0], clientY: end[1] })
}
const CX = CANVAS.x + CANVAS.size / 2
const CY = CANVAS.y + CANVAS.size / 2

describe('theme editor', () => {
  it('lays out the design: modes at the top of the dotted canvas, one handle, minus and plus, 9 swatches, the slider and dial', () => {
    setup()
    expect(dialog().style.width).toBe('356px')
    expect(dialog().style.height).toBe('508px')
    expect(screen.getAllByRole('radio').map((r) => [r.getAttribute('aria-label'), r.getAttribute('aria-checked')])).toEqual([
      ['Auto', 'true'], ['Light', 'false'], ['Dark', 'false']
    ])
    expect(canvas().contains(screen.getByRole('radiogroup', { name: 'Appearance' }))).toBe(true)
    expect(canvas().contains(screen.getByRole('button', { name: 'Add color' }))).toBe(true)
    expect(handles()).toHaveLength(1)
    expect(handles()[0]?.style.width).toBe('49px')
    // Hue 0 sits right of the center, half the wheel out.
    expect(handles()[0]?.style.left).toBe('170px')
    expect(handles()[0]?.style.top).toBe('170px')
    expect(handles()[0]?.style.transform).toMatch(/^translate\(75px(, 0px)?\)$/)
    expect((screen.getByRole('button', { name: 'Remove color' }) as HTMLButtonElement).disabled).toBe(true)
    expect(dialog().querySelectorAll('.clu-tp-swatch[aria-hidden="false"]')).toHaveLength(9)
    expect(dialog().querySelectorAll('.clu-tp-swatch')).toHaveLength(45)
    expect(screen.getByRole('slider', { name: 'Intensity' }).getAttribute('aria-valuenow')).toBe('0.5')
    expect(screen.getByRole('slider', { name: 'Grain' }).getAttribute('aria-valuetext')).toBe('30%')
    // Grain 0.3: the pin at 114 degrees; the dots from the top through 108 are lit.
    expect(dialog().querySelectorAll('.clu-tp-dot-on')).toHaveLength(7)
  })

  it('drags a handle around the wheel, previewing every move, and commits once on release', () => {
    const { calls, last } = setup()
    const h = handles()[0] as HTMLElement
    drag(h, [[CX + 75, CY], [CX, CY - 100], [CX - 150, CY]])
    const previews = calls('theme.previewSpaceTheme') as { theme: SpaceTheme }[]
    expect(previews.length).toBeGreaterThanOrEqual(1)
    expect(previews[0]?.theme.colors[0]).toEqual(canvasToColor({ x: 0, y: -100 }))
    expect(calls('theme.setSpaceTheme')).toHaveLength(1)
    // Past the edge the handle stops 22.5 pt inside the canvas.
    expect(last('theme.setSpaceTheme')?.theme.colors[0]).toEqual(canvasToColor({ x: -150, y: 0 }))
    expect(Number.parseFloat(/translate\((-?[\d.]+)px/.exec(handles()[0]?.style.transform ?? '')?.[1] ?? '')).toBeCloseTo(22.5 - 170, 0)
  })

  it('a press on a handle without a move picks it up and writes nothing', () => {
    const { calls } = setup()
    drag(handles()[0] as HTMLElement, [[CX + 75, CY]])
    expect(calls('theme.previewSpaceTheme')).toEqual([])
    expect(calls('theme.setSpaceTheme')).toEqual([])
  })

  it('a press on the bare canvas moves the main handle there', () => {
    const { last } = setup()
    drag(canvas(), [[CX, CY + 150]])
    expect(last('theme.setSpaceTheme')?.theme.colors[0]).toEqual(canvasToColor({ x: 0, y: 150 }))
  })

  // A space with no theme row (a color picked in New Space) opens with the handles of the theme its sidebar shows.
  it('a space with no theme opens with its default theme\'s handles, writes nothing, and a tap moves the main one', () => {
    const { calls, last } = setup(null as unknown as SpaceTheme)
    expect(handles()).toHaveLength(2)
    expect(dialog().getAttribute('data-colors')).toBe('2')
    expect(screen.queryByText('Tap to pick a color for this space')).toBeNull()
    expect(calls('theme.setSpaceTheme')).toEqual([])
    expect(calls('theme.previewSpaceTheme')).toEqual([])
    drag(canvas(), [[CX, CY + 150]])
    expect(last('theme.setSpaceTheme')?.theme.colors).toHaveLength(2)
    expect(last('theme.setSpaceTheme')?.theme.colors[0]).toEqual(canvasToColor({ x: 0, y: 150 }))
  })

  it('plus adds a smaller handle opposite the main one, then the -150 and +150 pair; minus takes one away', () => {
    const { last } = setup()
    const add = screen.getByRole('button', { name: 'Add color' }) as HTMLButtonElement
    fireEvent.click(add)
    expect(handles()).toHaveLength(2)
    expect(handles()[0]?.style.width).toBe('45px')
    expect(handles()[1]?.style.width).toBe('28px')
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual(harmony({ h: 0, s: 0.5 }, 2))
    fireEvent.click(add)
    expect(handles()).toHaveLength(3)
    expect(add.disabled).toBe(true)
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual([{ h: 0, s: 0.5 }, { h: 210, s: 0.5 }, { h: 150, s: 0.5 }])
    // Minus takes the last one away; the others stay where they are.
    fireEvent.click(screen.getByRole('button', { name: 'Remove color' }))
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual([{ h: 0, s: 0.5 }, { h: 210, s: 0.5 }])
    // Plus and minus change the handles within one frame: no glide, and no entry on the added handles.
    expect(canvas().hasAttribute('data-glide')).toBe(false)
    expect(dialog().querySelectorAll('.clu-tp-handle[data-enter]')).toHaveLength(0)
  })

  it('the first tap on an empty canvas: the hint fades out and the new handle plays its entry once', () => {
    const { echo } = setup({ colors: [], intensity: 0.5, grain: 0 })
    const hint = screen.getByText('Tap to pick a color for this space')
    expect(hint.hasAttribute('data-hidden')).toBe(false)
    drag(canvas(), [[CX + 75, CY]])
    expect(handles()).toHaveLength(1)
    // Still mounted, fading under the tap.
    expect(screen.getByText('Tap to pick a color for this space')).toBe(hint)
    expect(hint.hasAttribute('data-hidden')).toBe(true)
    const h = handles()[0] as HTMLElement
    expect(h.hasAttribute('data-enter')).toBe(true)
    // An echo re-renders the same node; the attribute stays, so the entry cannot restart.
    echo((s) => { const sp = s.spaces[SPACE]; if (sp) sp.theme = { colors: [canvasToColor({ x: 75, y: 0 })], intensity: 0.5, grain: 0 } })
    expect(handles()[0]).toBe(h)
  })

  it('handles present when the editor opens play no entry', () => {
    setup({ colors: harmony({ h: 0, s: 0.5 }, 3), intensity: 0.5, grain: 0 })
    expect(dialog().querySelectorAll('.clu-tp-handle[data-enter]')).toHaveLength(0)
    expect(screen.queryByText('Tap to pick a color for this space')).toBeNull()
  })

  it('dragging any handle moves only that handle; the others stay exactly where they are', () => {
    const three = harmony({ h: 0, s: 0.5 }, 3)
    const { last, calls } = setup({ colors: three, intensity: 0.5, grain: 0 })
    // The main one to north: the others neither turn nor change distance.
    drag(handles()[0] as HTMLElement, [[CX + 75, CY], [CX, CY - 100]])
    const north = canvasToColor({ x: 0, y: -100 })
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual([north, three[1], three[2]])
    // Every preview during the drag kept the others too.
    for (const p of calls('theme.previewSpaceTheme') as { theme: SpaceTheme }[]) expect(p.theme.colors.slice(1)).toEqual(three.slice(1))
    // Grabbing the third handle neither makes it the main one nor re-places the others; a press alone writes nothing.
    const writes = calls('theme.setSpaceTheme').length
    const third = handles()[2] as HTMLElement
    fireEvent.pointerDown(third, { button: 0, pointerId: 2, clientX: 0, clientY: 0 })
    fireEvent.pointerUp(third, { pointerId: 2, clientX: 0, clientY: 0 })
    expect(calls('theme.setSpaceTheme')).toHaveLength(writes)
    expect(handles()[0]?.style.width).toBe('45px')
    expect(handles()[2]?.style.width).toBe('28px')
    // Dragging the third moves only the third, right next to the main one.
    drag(handles()[2] as HTMLElement, [[CX, CY], [CX + 10, CY - 95]])
    const near = canvasToColor({ x: 10, y: -95 })
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual([north, three[1], near])
    // Arrow keys on the second nudge only the second.
    fireEvent.keyDown(handles()[1] as HTMLElement, { key: 'ArrowRight' })
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual([north, { h: 212, s: 0.5 }, near])
  })

  it('scaled down (onboarding), a pointer still lands on the color under it', () => {
    const { last } = setup()
    const scaled = proto.getBoundingClientRect as (this: HTMLElement) => DOMRect
    proto.getBoundingClientRect = function (this: HTMLElement) {
      const r = scaled.call(this)
      // The canvas at half size, same center.
      if (!this.classList.contains('clu-tp-canvas')) return r
      const w = r.width / 2
      return { ...r, left: CX - w / 2, top: CY - w / 2, width: w, height: w, x: CX - w / 2, y: CY - w / 2, right: CX + w / 2, bottom: CY + w / 2 }
    }
    // 37.5 px east on a half-size canvas is the color at 75 pt east.
    drag(canvas(), [[CX + 37.5, CY]])
    expect(last('theme.setSpaceTheme')?.theme.colors[0]).toEqual(canvasToColor({ x: 75, y: 0 }))
  })

  it('slides the swatch pages without wrapping, and a swatch replaces the theme with that one color', () => {
    const { last } = setup({ colors: harmony({ h: 0, s: 0.5 }, 3), intensity: 0.5, grain: 0 })
    const prev = screen.getByRole('button', { name: 'Previous presets' }) as HTMLButtonElement
    const next = screen.getByRole('button', { name: 'Next presets' }) as HTMLButtonElement
    expect(prev.disabled).toBe(true)
    const strip = dialog().querySelector('.clu-tp-strip') as HTMLElement
    // A page slides in 330 ms on the strip's curve.
    expect(SWATCHES.slideMs).toBe(330)
    expect(strip.style.transitionDuration).toBe('330ms')
    expect(strip.style.transitionTimingFunction).toBe(SWATCH_SLIDE_EASING)
    for (let i = 1; i < SWATCH_PAGES.length; i++) fireEvent.click(next)
    expect(next.disabled).toBe(true)
    expect(prev.disabled).toBe(false)
    expect((dialog().querySelector('.clu-tp-strip') as HTMLElement).style.transform).toBe(`translateX(${-(SWATCH_PAGES.length - 1) * 265.5}px)`)
    for (let i = 1; i < SWATCH_PAGES.length; i++) fireEvent.click(prev)
    expect((dialog().querySelector('.clu-tp-strip') as HTMLElement).style.transform).toBe('translateX(0px)')
    const pink = SWATCH_PAGES[0]?.colors[1] as string
    const swatch = dialog().querySelector(`[data-swatch="${pink}"]`) as HTMLElement
    fireEvent.click(swatch)
    expect(last('theme.setSpaceTheme')?.theme.colors).toEqual([swatchColor(pink)])
    expect(swatch.getAttribute('aria-pressed')).toBe('true')
  })

  it('a tap or a swatch slides the handles, a drag moves them under the pointer; swatches carry the set names', () => {
    setup()
    drag(canvas(), [[100, 120]])
    expect(canvas().hasAttribute('data-glide')).toBe(true)
    drag(canvas(), [[100, 120], [140, 160]])
    expect(canvas().hasAttribute('data-glide')).toBe(false)
    const swatch = screen.getByRole('button', { name: 'Brand 2' })
    fireEvent.click(swatch)
    expect(canvas().hasAttribute('data-glide')).toBe(true)
    expect(swatch.getAttribute('aria-pressed')).toBe('true')
    expect(dialog().querySelectorAll('.clu-tp-swatch[aria-pressed="true"]')).toHaveLength(1)
  })

  it('the wavy slider follows the pointer continuously, its thumb grows, and it commits on release', () => {
    const { calls, last } = setup()
    const slider = screen.getByRole('slider', { name: 'Intensity' })
    drag(slider, [[SLIDER.x + 60, 452], [SLIDER.x + 139.3, 452]])
    expect((calls('theme.previewSpaceTheme') as { theme: SpaceTheme }[])[0]?.theme.intensity).toBe(sliderValue(60))
    expect(last('theme.setSpaceTheme')?.theme.intensity).toBe(sliderValue(139.3))
    expect(slider.getAttribute('aria-valuenow')).toBe(String(sliderValue(139.3)))
    fireEvent.keyDown(slider, { key: 'End' })
    expect(last('theme.setSpaceTheme')?.theme.intensity).toBe(1)
    expect((slider.querySelector('.clu-tp-thumb') as HTMLElement).style.height).toBe('59px')
    fireEvent.keyDown(slider, { key: 'Home' })
    expect((slider.querySelector('.clu-tp-thumb') as HTMLElement).style.height).toBe('48px')
  })

  it('the grain dial turns clockwise from the top, continuously, and wraps past it back to the least grain', () => {
    const { last } = setup({ colors: [{ h: 0, s: 0.5 }], intensity: 0.5, grain: 0.9 })
    const dial = screen.getByRole('slider', { name: 'Grain' })
    // Press near the end, then drag across the top: it wraps to the start of the scale.
    drag(dial, [[298.5 - 30, 452.5 - 5], [298.5 + 12, 452.5 - 30]])
    expect(last('theme.setSpaceTheme')?.theme.grain).toBe(dialValue(12, -30))
    expect(last('theme.setSpaceTheme')?.theme.grain).toBeLessThan(0.05)
    drag(dial, [[298.5 + 30, 452.5]])
    expect(last('theme.setSpaceTheme')?.theme.grain).toBe(0.227)
    fireEvent.keyDown(dial, { key: 'ArrowRight' })
    expect(last('theme.setSpaceTheme')?.theme.grain).toBe(0.3)
  })

  it('the mode buttons switch the appearance', () => {
    const { calls } = setup()
    fireEvent.click(screen.getByRole('radio', { name: 'Dark' }))
    expect(calls('theme.setMode')).toEqual([{ mode: 'dark' }])
  })

  it('opens exactly once: shown after the first style pass and never again from a re-render or a state echo', () => {
    const { echo } = setup()
    const root = dialog()
    expect(root.hasAttribute('data-shown')).toBe(true)
    echo((s) => void (s.app.appearance.mode = 'dark'))
    echo((s) => { const sp = s.spaces[SPACE]; if (sp) sp.theme = { colors: [{ h: 120, s: 0.5 }], intensity: 0.5, grain: 0.3 } })
    // Same node, attribute never dropped, so the opening transition cannot restart.
    expect(dialog()).toBe(root)
    expect(root.hasAttribute('data-shown')).toBe(true)
    expect(root.hasAttribute('data-closing')).toBe(false)
  })

  it("a mode icon turns only for the click that picked it, never on open or for a change made elsewhere", () => {
    const { echo } = setup()
    const picked = () => [...dialog().querySelectorAll('.clu-tp-mode[data-picked]')].map((b) => b.getAttribute('data-mode'))
    expect(picked()).toEqual([])
    echo((s) => void (s.app.appearance.mode = 'dark'))
    expect(picked()).toEqual([])
    fireEvent.click(screen.getByRole('radio', { name: 'Light' }))
    echo((s) => void (s.app.appearance.mode = 'light'))
    const light = screen.getByRole('radio', { name: 'Light' })
    expect(picked()).toEqual(['light'])
    // The echo and later renders keep the attribute on the same node: the turn plays once.
    echo((s) => void (s.app.appearance.mode = 'light'))
    expect(screen.getByRole('radio', { name: 'Light' })).toBe(light)
    expect(picked()).toEqual(['light'])
    // Clicking the mode already on does not play it again.
    fireEvent.click(screen.getByRole('radio', { name: 'Auto' }))
    echo((s) => void (s.app.appearance.mode = 'auto'))
    expect(picked()).toEqual(['auto'])
  })

  it('Esc shrinks the editor toward the sidebar, then closes it; a drag after Esc writes nothing', () => {
    vi.useFakeTimers()
    const { calls } = setup()
    // The stub's first render has no state yet, which closes once; count from here.
    const before = calls('overlay.close').length
    fireEvent.keyDown(dialog(), { key: 'Escape' })
    expect(dialog().hasAttribute('data-closing')).toBe(true)
    expect(calls('overlay.close')).toHaveLength(before)
    drag(canvas(), [[CX, CY]])
    expect(calls('theme.setSpaceTheme')).toEqual([])
    // 150 ms from Esc to gone.
    expect(CLOSE_MS).toBe(150)
    act(() => void vi.advanceTimersByTime(CLOSE_MS - 1))
    expect(calls('overlay.close')).toHaveLength(before)
    act(() => void vi.advanceTimersByTime(1))
    expect(calls('overlay.close').slice(before)).toEqual([{ instanceId: 'i1' }])
  })

  it('opens 13 pt right of the sidebar at the anchor\'s top', () => {
    setup(undefined, { sidebarWidth: 300 })
    expect(dialog().style.left).toBe(`${300 + PICKER_GAP}px`)
    expect(dialog().style.top).toBe('60px')
  })

  it('opens at the window margin when the sidebar is hidden', () => {
    setup(undefined, { hidden: true })
    expect(dialog().style.left).toBe('8px')
  })
})
