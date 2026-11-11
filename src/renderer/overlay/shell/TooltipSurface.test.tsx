import { render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { OverlaySurfaceState } from '../../../shared/state'
import { TooltipSurface } from './TooltipSurface'

const WIN = '00000000-0000-4000-8000-000000000001'
const instance = (props: unknown): OverlaySurfaceState =>
  ({ instanceId: '00000000-0000-4000-8000-0000000000aa', surface: 'tooltip', layer: 'passive', modality: 'passive', rect: { x: 0, y: 0, width: 120, height: 52 }, openedFrom: null, focus: false, slot: 2, props }) as OverlaySurfaceState

describe('TooltipSurface', () => {
  it('draws the frame pill with its keycaps, its corner and its fade', () => {
    render(<TooltipSurface instance={instance({ label: 'Go back', keys: ['⌘', '['], above: true, closing: true })} windowId={WIN} />)
    const tip = screen.getByRole('tooltip')
    expect(tip).toHaveTextContent('Go back')
    expect([...tip.querySelectorAll('kbd')].map((k) => k.textContent)).toEqual(['⌘', '['])
    expect(tip).toHaveAttribute('data-above')
    expect(tip).toHaveAttribute('data-closing')
  })

  it('draws nothing for junk props and skips junk keys', () => {
    const { container } = render(<TooltipSurface instance={instance(null)} windowId={WIN} />)
    expect(container).toBeEmptyDOMElement()
    render(<TooltipSurface instance={instance({ label: 'Reload', keys: [1, null, 'R'] })} windowId={WIN} />)
    expect([...screen.getByRole('tooltip').querySelectorAll('kbd')].map((k) => k.textContent)).toEqual(['R'])
  })
})
