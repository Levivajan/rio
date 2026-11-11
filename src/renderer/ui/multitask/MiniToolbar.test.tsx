import { act, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { MiniWindowState } from '../../../shared/state/multitask'
import { stubClu } from '../../../test-utils/stubClu'
import { MiniToolbar } from './MiniToolbar'
import { PROGRESS_LINGER_MS } from './useProgressBar'

const base: MiniWindowState = {
  tabId: 'page-1', targetSpaceId: 's1', targetSpaceName: 'Work', profileMismatch: false, confirming: false,
  url: 'https://www.example.com/a', title: 'Example', favicon: null, loading: false, progress: 1, canGoBack: false
}

let stub: ReturnType<typeof stubClu>
beforeEach(() => {
  stub = stubClu({ 'mini.back': () => null, 'mini.moveToSpace': () => null, 'mini.pickSpace': () => null, 'window.close': () => null })
  window.clu = stub.clu
})
afterEach(() => vi.useRealTimers())

const bar = () => document.querySelector<HTMLElement>('[data-mini-progress]')

describe('MiniToolbar', () => {
  it('shows Back, the host, Site controls, Open in the target space and Close, and no extensions row', () => {
    render(<MiniToolbar mini={base} />)
    expect(screen.getByRole('button', { name: 'Back' })).toBeDisabled()
    expect(document.querySelector('[data-mini-host]')?.textContent).toBe('example.com')
    expect(document.querySelector('[data-slot="action-row"]')).toBeNull()
    expect(document.querySelector('.clu-action-row')).toBeNull()
    expect(document.querySelector('[data-slot="site-controls"]')).not.toBeNull()
    expect(screen.getByRole('button', { name: 'Open in Work' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Close' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: 'Forward' })).toBeNull()
  })

  it('sends mini.back, mini.moveToSpace and window.close', () => {
    render(<MiniToolbar mini={{ ...base, canGoBack: true }} />)
    fireEvent.click(screen.getByRole('button', { name: 'Back' }))
    fireEvent.click(screen.getByRole('button', { name: 'Open in Work' }))
    fireEvent.click(screen.getByRole('button', { name: 'Close' }))
    expect(stub.calls.map((c) => c.name)).toEqual(['mini.back', 'mini.moveToSpace', 'window.close'])
    expect(stub.calls[1]?.payload).toEqual({})
  })

  it('the caret next to Open in sends mini.pickSpace', () => {
    render(<MiniToolbar mini={base} />)
    fireEvent.click(screen.getByRole('button', { name: 'Choose a space' }))
    expect(stub.calls.map((c) => c.name)).toEqual(['mini.pickSpace'])
  })

  it('shows the favicon image when there is one, else a letter tile', () => {
    const { rerender } = render(<MiniToolbar mini={base} />)
    expect(document.querySelector('img[data-mini-favicon]')).toBeNull()
    expect(document.querySelector('[data-mini-favicon]')?.textContent).toBe('E')
    rerender(<MiniToolbar mini={{ ...base, favicon: 'clu://favicon/abc' }} />)
    expect(document.querySelector('img[data-mini-favicon]')?.getAttribute('src')).toBe('clu://favicon/abc')
  })

  it('labels about:blank and junk URLs without crashing', () => {
    const { rerender } = render(<MiniToolbar mini={{ ...base, url: 'about:blank', title: '' }} />)
    expect(document.querySelector('[data-mini-host]')?.textContent).toBe('New page')
    rerender(<MiniToolbar mini={{ ...base, url: 'not a url', title: 'T'.repeat(5000) }} />)
    expect(document.querySelector('[data-mini-host]')?.textContent).toHaveLength(5000)
    rerender(<MiniToolbar mini={null} />)
    expect(screen.getByRole('button', { name: 'Open in Space' })).toBeInTheDocument()
  })

  it('shows a 2 px progress bar while loading and removes it shortly after the load ends', () => {
    vi.useFakeTimers()
    const { rerender } = render(<MiniToolbar mini={{ ...base, loading: true, progress: 0.4 }} />)
    expect(bar()?.className).toContain('h-[2px]')
    expect(bar()?.style.width).toBe('40%')
    expect(bar()?.getAttribute('aria-valuenow')).toBe('40')
    rerender(<MiniToolbar mini={{ ...base, loading: false, progress: 1 }} />)
    expect(bar()?.style.width).toBe('100%')
    expect(bar()?.style.opacity).toBe('0')
    act(() => void vi.advanceTimersByTime(PROGRESS_LINGER_MS))
    expect(bar()).toBeNull()
    expect(PROGRESS_LINGER_MS).toBeLessThan(400)
  })

  it('clamps junk progress values', () => {
    render(<MiniToolbar mini={{ ...base, loading: true, progress: Number.NaN }} />)
    expect(bar()?.style.width).toBe('0%')
  })

  it('keeps the lights spacer whole and lets the Open in label truncate instead of clipping', () => {
    render(<MiniToolbar mini={{ ...base, targetSpaceName: 'A very long space name that cannot fit' }} />)
    expect(document.querySelector<HTMLElement>('.clu-traffic-spacer')?.style.flexShrink).toBe('0')
    expect(document.querySelector('[data-mini-open-group]')?.className).not.toContain('overflow-hidden')
    expect(document.querySelector('[data-mini-open-in]')?.className).toContain('truncate')
  })
})
