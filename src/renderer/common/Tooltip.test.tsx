import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { LOOK } from '../../shared/look/tokens'
import { stubClu } from '../../test-utils/stubClu'
import { drawTooltipsInOverlay, TIP_MARGIN, Tooltip } from './Tooltip'

beforeEach(() => vi.useFakeTimers())
afterEach(() => vi.useRealTimers())

function hover(x: number, y: number) {
  const anchor = document.querySelector('.clu-tooltip-anchor') as HTMLElement
  fireEvent.pointerEnter(anchor, { clientX: x, clientY: y })
  act(() => vi.advanceTimersByTime(LOOK.tooltip.delayMs))
  return anchor
}

describe('Tooltip', () => {
  it('hangs 9.5 right of and below the pointer after 450 ms, with the shortcut as one keycap per key', () => {
    render(
      <Tooltip label="Hide sidebar" command="sidebar.toggle">
        <button type="button">t</button>
      </Tooltip>
    )
    hover(40, 20)
    const tip = screen.getByRole('tooltip')
    expect(tip).toHaveTextContent('Hide sidebar')
    expect(tip.style.left).toBe(`${40 + LOOK.tooltip.offset}px`)
    expect(tip.style.top).toBe(`${20 + LOOK.tooltip.offset}px`)
    expect([...tip.querySelectorAll('kbd')].map((k) => k.textContent)).toEqual(['⌘', 'S'])
  })

  it("puts a space's profile chip between the name and the ⌃1 keycaps", () => {
    render(
      <Tooltip label="Main" chip="Personal" command="spaces.goto1">
        <button type="button">t</button>
      </Tooltip>
    )
    hover(0, 0)
    const tip = screen.getByRole('tooltip')
    expect(tip.querySelector('.clu-tooltip-chip')).toHaveTextContent('Personal')
    expect([...tip.querySelectorAll('kbd')].map((k) => k.textContent)).toEqual(['⌃', '1'])
    expect(tip.firstChild?.textContent).toBe('Main')
  })

  it('shows no keycaps for a command without a shortcut or an unknown one', () => {
    render(
      <Tooltip label="Nothing" command="no.such.command">
        <button type="button">t</button>
      </Tooltip>
    )
    hover(0, 0)
    expect(screen.getByRole('tooltip').querySelector('kbd')).toBeNull()
  })

  it('hangs above the pointer when the scrolling panel it sits in would cut it off (Zoom in)', () => {
    render(
      <div className="panel" style={{ overflowY: 'auto' }}>
        <Tooltip label="Zoom in">
          <button type="button">+</button>
        </Tooltip>
      </div>
    )
    const rect = (top: number, bottom: number) => ({ top, bottom, left: 0, right: 40, width: 40, height: bottom - top, x: 0, y: top, toJSON: () => ({}) })
    const spy = vi.spyOn(Element.prototype, 'getBoundingClientRect').mockImplementation(function (this: Element) {
      if (this.classList.contains('panel')) return rect(0, 300) as DOMRect
      if (this.getAttribute('role') === 'tooltip') return rect(290, 318) as DOMRect
      return rect(270, 290) as DOMRect
    })
    const height = vi.spyOn(document.documentElement, 'clientHeight', 'get').mockReturnValue(900)
    const width = vi.spyOn(document.documentElement, 'clientWidth', 'get').mockReturnValue(900)
    hover(10, 280)
    expect(screen.getByRole('tooltip')).toHaveAttribute('data-above')
    spy.mockRestore()
    height.mockRestore()
    width.mockRestore()
  })

  it('fades out over the out time instead of vanishing, then unmounts', () => {
    render(
      <Tooltip label="Back">
        <button type="button">t</button>
      </Tooltip>
    )
    const anchor = hover(10, 10)
    fireEvent.pointerLeave(anchor)
    expect(screen.getByRole('tooltip')).toHaveAttribute('data-closing')
    act(() => vi.advanceTimersByTime(LOOK.tooltip.outMs + 60))
    expect(screen.queryByRole('tooltip')).toBeNull()
  })
})

describe('Tooltip in the frame', () => {
  let stub: ReturnType<typeof stubClu>
  beforeEach(() => {
    drawTooltipsInOverlay(true)
    stub = stubClu({ 'shell.tooltip': () => null })
    window.clu = stub.clu
  })
  afterEach(() => drawTooltipsInOverlay(false))
  const tips = () => stub.calls.filter((c) => c.name === 'shell.tooltip').map((c) => c.payload as Record<string, unknown>)

  it('hands the laid-out pill to the overlay, which draws it above the page card, and hides the frame copy', async () => {
    render(
      <Tooltip label="Go back" command="nav.back">
        <button type="button">t</button>
      </Tooltip>
    )
    const anchor = hover(40, 20)
    await act(async () => {})
    const frameTip = screen.getByRole('tooltip')
    expect(frameTip).toHaveAttribute('data-remote')
    const shown = tips().at(-1)
    expect(shown).toMatchObject({ label: 'Go back', keys: ['⌘', '['], closing: false })
    const rect = shown?.rect as { x: number; y: number; width: number; height: number }
    const r = frameTip.getBoundingClientRect()
    expect(rect).toEqual({ x: Math.round(r.left - TIP_MARGIN), y: Math.round(r.top - TIP_MARGIN), width: Math.ceil(r.width + 2 * TIP_MARGIN), height: Math.ceil(r.height + 2 * TIP_MARGIN) })
    fireEvent.pointerLeave(anchor)
    await act(async () => {})
    expect(tips().at(-1)).toMatchObject({ label: 'Go back', closing: true })
    act(() => vi.advanceTimersByTime(LOOK.tooltip.outMs + 60))
    await act(async () => {})
    expect(tips().at(-1)).toEqual({})
  })

  it('hides the overlay pill when its button goes away while it shows', async () => {
    const view = render(
      <Tooltip label="Reload">
        <button type="button">t</button>
      </Tooltip>
    )
    hover(10, 10)
    await act(async () => {})
    expect(tips().at(-1)).toMatchObject({ label: 'Reload' })
    view.unmount()
    expect(tips().at(-1)).toEqual({})
  })

  it('draws the pill in the frame again when the browser refuses the overlay', async () => {
    stub = stubClu()
    window.clu = stub.clu
    render(
      <Tooltip label="Forward">
        <button type="button">t</button>
      </Tooltip>
    )
    hover(10, 10)
    await act(async () => {})
    await act(async () => {})
    expect(screen.getByRole('tooltip')).not.toHaveAttribute('data-remote')
  })
})
