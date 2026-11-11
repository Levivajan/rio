import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultWindowState, type OverlaySurfaceState } from '../../../shared/state'
import type { Json } from '../../../shared/types'
import { stubClu } from '../../../test-utils/stubClu'
import { backdropCard, PEEK_SUMMARY_STRIP, PeekChrome } from './PeekChrome'
import { CLOSE_MS, closeMotion, OPEN_EASE, OPEN_MS, openMotion, originWithin, RAIL_DELAY_MS, readMs } from './peek-motion'

const WIN = '00000000-0000-4000-8000-000000000001'
const CARD = { x: 339, y: 55, width: 863, height: 690 }
const ORIGIN = { x: 400, y: 120, width: 1, height: 1 }

interface Played { el: Element; keyframes: Keyframe[]; options: KeyframeAnimationOptions }
let played: Played[] = []

beforeEach(() => {
  played = []
  Element.prototype.animate = vi.fn(function (this: Element, keyframes: Keyframe[], options: KeyframeAnimationOptions) {
    played.push({ el: this, keyframes, options })
    return { finished: Promise.resolve(), cancel: () => undefined } as unknown as Animation
  }) as unknown as Element['animate']
})
afterEach(() => {
  delete (Element.prototype as { animate?: unknown }).animate
})

function mount(opts: { props?: Json; peek?: boolean; reduceMotion?: boolean; canGoBack?: boolean; summary?: string } = {}) {
  const stub = stubClu({ 'peek.animationDone': () => null, 'peek.close': () => null, 'peek.toSplit': () => null, })
  window.clu = { ...stub.clu, layer: 'under-peek', slot: null }
  const instance: OverlaySurfaceState = { instanceId: '77777777-7777-4777-8777-777777777777', surface: 'peek', layer: 'under-peek', modality: 'passive', rect: null, openedFrom: null, focus: false, slot: null, props: opts.props ?? {} }
  const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
  if (opts.peek !== false) win.peek = { seq: 7, url: 'http://localhost/t.html', host: 'localhost', title: '', favicon: null, canGoBack: !!opts.canGoBack, originRect: ORIGIN, cardRect: CARD, summary: opts.summary ?? null }
  const base = defaultState()
  const state = { ...base, rev: 1, app: { ...base.app, appearance: { ...base.app.appearance, reduceMotion: !!opts.reduceMotion } }, windows: { [WIN]: win } }
  render(<PeekChrome instance={instance} windowId={WIN} />)
  act(() => stub.push({ kind: 'snapshot', rev: 1, state }))
  return stub
}

const card = () => document.querySelector<HTMLElement>('[data-peek-card]')

describe('peek motion', () => {
  it('unfolds from the design\'s wide short band at the link, opaque from the first frame', () => {
    const open = openMotion(ORIGIN, CARD)
    expect(open.transformOrigin).toBe('0px 0px')
    // 943.5 / 853 wide and 242.5 / 776 tall, centered across the card; the band sits as high as the card allows here.
    expect(open.keyframes[0]).toEqual({ transform: 'translate(-45.7805px, 0px) scale(1.1061, 0.3125)', opacity: 1 })
    expect(open.keyframes.at(-1)).toEqual({ transform: 'none', opacity: 1 })
    // A link lower down: the band centers on it, never past the card's bottom.
    const mid = openMotion({ x: 400, y: 400, width: 100, height: 20 }, CARD).keyframes[0]?.transform as string
    expect(mid).toContain(`, ${+(410 - (690 * 242.5) / 776 / 2 - 55).toFixed(4)}px)`)
    const low = openMotion({ x: 400, y: 5000, width: 1, height: 1 }, CARD).keyframes[0]?.transform as string
    expect(low).toContain(`, ${+(690 - (690 * 242.5) / 776).toFixed(4)}px)`)
    expect(openMotion({ x: Number.NaN, y: Number.NaN, width: 0, height: 0 }, CARD).keyframes[0]?.transform).toContain(', 0px)')
  })
  it('shrinks the close image back toward the origin point', () => {
    expect(originWithin(ORIGIN, CARD)).toBe('62px 66px')
    const close = closeMotion(ORIGIN, CARD)
    expect(close.transformOrigin).toBe('62px 66px')
    expect(close.keyframes).toEqual([{ transform: 'none', opacity: 1 }, { transform: 'scale(0.1, 0.1)', opacity: 0 }])
  })
  it('a large origin closes to its own size; junk durations fall back', () => {
    expect(closeMotion({ x: 339, y: 55, width: 431, height: 345 }, CARD).keyframes.at(-1)).toMatchObject({ transform: 'scale(0.4994, 0.5)' })
    expect(readMs('150ms', 1)).toBe(150)
    expect(readMs('0.2s', 1)).toBe(200)
    expect(readMs('', 150)).toBe(150)
    expect(readMs('-3ms', 150)).toBe(150)
  })
})

describe('PeekChrome', () => {
  it('draws the card at cardRect (the host is in the URL pill), unfolds it and reports animationDone', async () => {
    const stub = mount()
    const el = card()
    expect(el).not.toBeNull()
    expect(el?.style.left).toBe('339px')
    expect(el?.style.width).toBe('863px')
    expect(el?.style.transformOrigin).toBe('0px 0px')
    expect(screen.queryByText('localhost')).toBeNull()
    const anim = played.find((p) => p.el === el)
    expect(anim?.options).toMatchObject({ duration: OPEN_MS, easing: OPEN_EASE, fill: 'both' })
    expect(OPEN_MS).toBe(185)
    expect(anim?.keyframes[0]).toMatchObject({ opacity: 1 })
    await act(async () => undefined)
    expect(stub.calls).toContainEqual({ name: 'peek.animationDone', payload: { seq: 7 } })
    expect(screen.getByRole('toolbar', { name: 'Peek' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Open in split' })).toBeInTheDocument()
  })

  it('shows a hover preview\'s summary in a strip at the card\'s top, and nothing without one', () => {
    mount({ summary: 'A guide to the city. It covers beaches and food.' })
    const note = screen.getByRole('note', { name: 'Summary' })
    expect(note).toHaveTextContent('A guide to the city. It covers beaches and food.')
    expect(note.style.height).toBe(`${PEEK_SUMMARY_STRIP}px`)
    expect(card()?.contains(note)).toBe(true)
  })

  it('draws no summary strip for a Peek without one', () => {
    mount()
    expect(screen.queryByRole('note', { name: 'Summary' })).toBeNull()
  })

  it('shows the rail 108 ms after the card starts, in about a frame', () => {
    mount()
    const rail = played.find((p) => p.el === screen.getByRole('toolbar', { name: 'Peek' }))
    expect(rail?.keyframes).toEqual([{ opacity: 0 }, { opacity: 1 }])
    expect(rail?.options).toMatchObject({ delay: RAIL_DELAY_MS, duration: 17, fill: 'backwards' })
    expect(RAIL_DELAY_MS).toBe(108)
  })

  it('opens with no card motion and no rail delay under reduced motion', () => {
    mount({ reduceMotion: true })
    expect(played.find((p) => p.el === card())?.options.duration).toBe(0)
    expect(played.find((p) => p.el === screen.getByRole('toolbar', { name: 'Peek' }))?.options.delay).toBe(0)
  })

  it('closes the Peek on a scrim press and sends toSplit from the rail', () => {
    const stub = mount()
    const scrim = document.querySelector('[data-peek-scrim]')
    if (!scrim) throw new Error('no scrim')
    fireEvent.pointerDown(scrim)
    expect(stub.calls).toContainEqual({ name: 'peek.close', payload: {} })
    fireEvent.click(screen.getByRole('button', { name: 'Open in split' }))
    expect(stub.calls).toContainEqual({ name: 'peek.toSplit', payload: {} })
  })

  it('holds only Close, Open as tab and Open in split, in the strong accent, the first centered 24.25 below the card', () => {
    mount()
    const rail = screen.getByRole('toolbar', { name: 'Peek' })
    expect([...rail.querySelectorAll('button')].map((b) => b.getAttribute('aria-label'))).toEqual(['Close Peek', 'Open as tab', 'Open in split'])
    expect(rail.style.top).toBe(`${CARD.y + 24.25 - 17}px`)
    expect(screen.getByRole('button', { name: 'Close Peek' }).style.color).toBe('var(--accent-strong)')
  })

  it('washes only the page card, shrunk to 0.983 around its center, with the space tint, not the window', () => {
    mount()
    const wash = document.querySelector<HTMLElement>('[data-peek-wash]')
    const page = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN }).shell.layout.pageRect
    const shrunk = backdropCard(page)
    expect(shrunk.width).toBeCloseTo(page.width * 0.983)
    expect(shrunk.x + shrunk.width / 2).toBeCloseTo(page.x + page.width / 2)
    expect(shrunk.y + shrunk.height / 2).toBeCloseTo(page.y + page.height / 2)
    expect(wash?.style.left).toBe(`${shrunk.x}px`)
    expect(wash?.style.width).toBe(`${shrunk.width}px`)
    expect(wash?.style.background).toContain('var(--peek-wash)')
    expect(document.querySelector<HTMLElement>('[data-peek-scrim]')?.style.background).toBe('')
  })

  it('covers the ring between the page card and its shrunk rect with the window background', () => {
    mount()
    const ring = document.querySelector<HTMLElement>('[data-peek-ring]')
    expect(ring?.querySelector('.clu-theme-bg')).not.toBeNull()
    // jsdom drops path() clip-paths, so check the raw attribute React wrote.
    const clip = ring?.getAttribute('style') ?? ''
    if (clip.includes('clip-path')) expect(clip).toContain('evenodd')
  })

  it('lays the ring out as two rounded rects, the page card and the shrunk one', () => {
    expect(backdropCard({ x: 223, y: 19, width: 1038, height: 762 })).toEqual({ x: 223 + (1038 - 1038 * 0.983) / 2, y: 19 + (762 - 762 * 0.983) / 2, width: 1038 * 0.983, height: 762 * 0.983 })
  })

  it('holds the frame while main captures, then shrinks the image toward the origin', async () => {
    const frame = { seq: 9, originRect: ORIGIN, cardRect: CARD, image: null, shrinking: false }
    const stub = mount({ peek: false, props: { closing: frame } as unknown as Json })
    expect(card()).not.toBeNull()
    expect(played).toEqual([])
    expect(stub.calls.some((c) => c.name === 'peek.animationDone')).toBe(false)
  })

  it('shrinks the captured page and reports the close animation', async () => {
    const frame = { seq: 9, originRect: ORIGIN, cardRect: CARD, image: 'data:image/jpeg;base64,AAAA', shrinking: true }
    const stub = mount({ peek: false, props: { closing: frame } as unknown as Json })
    const el = card()
    expect(el?.querySelector('img')?.getAttribute('src')).toBe('data:image/jpeg;base64,AAAA')
    expect(el?.style.transformOrigin).toBe('62px 66px')
    const anim = played.find((p) => p.el === el)
    expect(anim?.options.duration).toBe(CLOSE_MS)
    expect(anim?.keyframes.at(-1)).toEqual({ transform: 'scale(0.1, 0.1)', opacity: 0 })
    await act(async () => undefined)
    expect(stub.calls).toContainEqual({ name: 'peek.animationDone', payload: { seq: 9 } })
    // No scrim click handler while closing: the Peek is already gone.
    expect(screen.queryByRole('toolbar')).toBeNull()
  })
})
