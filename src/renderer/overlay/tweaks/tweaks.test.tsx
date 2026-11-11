// TweaksSection and TweakEditor against a stub window.clu (tweaks-t1 to t3).
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { defaultState, defaultTabState, defaultWindowState, type CluState } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import { DEFAULT_TWEAK_CONTROLS } from '../../../shared/tweaks'
import { hueText } from './StylePanel'
import { MAX_CODE_BYTES, PREVIEW_MS, TweakEditor } from './TweakEditor'
import { TweaksSection } from './TweaksSection'

const TAB = '22222222-2222-4222-8222-222222222221'
const INST = '77777777-7777-4777-8777-777777777777'

function snapshot(tweak: CluState['tabs'][string]['tweak'], url = 'http://www.a.test/x'): CluState {
  const tab = { ...defaultTabState({ id: TAB, profileId: TAB, spaceId: null, section: 'today', url, title: '', lastActiveAt: 0 }), tweak }
  return { ...defaultState(), rev: 1, tabs: { [TAB]: tab } }
}

function mount(state: CluState, overrides: Record<string, (p: unknown) => unknown> = {}) {
  const stub = stubClu(overrides)
  window.clu = stub.clu
  return { stub, push: () => act(() => stub.push({ kind: 'snapshot', rev: state.rev, state })) }
}

afterEach(() => vi.useRealTimers())

describe('TweaksSection', () => {
  it('turns an empty site on from the switch and opens the editor from Edit CSS & JS', () => {
    const { stub, push } = mount(snapshot(null))
    render(<TweaksSection tabId={TAB} />)
    push()
    expect(screen.getByText('No tweaks for a.test')).toBeInTheDocument()
    const sw = screen.getByRole('switch', { name: 'Site tweaks on' })
    expect(sw).toHaveAttribute('aria-checked', 'false')
    fireEvent.click(sw)
    expect(stub.calls).toContainEqual({ name: 'tweaks.setEnabled', payload: { tabId: TAB, enabled: true } })
    expect(stub.calls.some((c) => c.name === 'tweaks.openEditor')).toBe(false)
    // The browser answers with an empty tweak, on.
    act(() => stub.push({ kind: 'snapshot', rev: 2, state: { ...snapshot({ enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount: 0 }), rev: 2 } }))
    expect(screen.getByRole('switch', { name: 'Site tweaks on' })).toHaveAttribute('aria-checked', 'true')
    expect(screen.getByText('On for a.test')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'Edit CSS & JS' }))
    expect(stub.calls.filter((c) => c.name === 'tweaks.openEditor')).toHaveLength(1)
  })

  it('shows what the tweak holds and toggles it', () => {
    const { stub, push } = mount(snapshot({ enabled: true, hasCss: true, hasJs: true, hasControls: false, zapCount: 0 }))
    render(<TweaksSection tabId={TAB} />)
    push()
    expect(screen.getByText('On · CSS, JS')).toBeInTheDocument()
    const sw = screen.getByRole('switch', { name: 'Site tweaks on' })
    expect(sw).toHaveAttribute('aria-checked', 'true')
    fireEvent.click(sw)
    expect(stub.calls).toContainEqual({ name: 'tweaks.setEnabled', payload: { tabId: TAB, enabled: false } })
  })

  it('starts zap mode and, once something is hidden, restores all', () => {
    const { stub, push } = mount(snapshot({ enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount: 0 }))
    const { rerender } = render(<TweaksSection tabId={TAB} />)
    push()
    expect(screen.queryByRole('button', { name: 'Restore all' })).toBeNull()
    fireEvent.click(screen.getByRole('button', { name: 'Zap elements' }))
    expect(stub.calls).toContainEqual({ name: 'tweaks.startZap', payload: { tabId: TAB } })
    act(() => stub.push({ kind: 'snapshot', rev: 2, state: { ...snapshot({ enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount: 2 }), rev: 2 } }))
    rerender(<TweaksSection tabId={TAB} />)
    expect(screen.getByText('2 hidden elements')).toBeInTheDocument()
    expect(screen.getByText('On · 2 hidden')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: 'Restore all' }))
    expect(stub.calls).toContainEqual({ name: 'tweaks.clearZaps', payload: { tabId: TAB } })
  })

  it('renders nothing on pages without a site', () => {
    const { push } = mount(snapshot(null, 'rio://settings'))
    const { container } = render(<TweaksSection tabId={TAB} />)
    push()
    expect(container).toBeEmptyDOMElement()
  })
})

describe('TweakEditor', () => {
  const instance = { instanceId: INST, surface: 'tweak-editor', layer: 'passive' as const, modality: 'passive' as const, rect: null, openedFrom: null, focus: true, slot: 0, props: { tabId: TAB, site: 'a.test' } }
  const saved = { site: 'a.test', enabled: true, css: 'h1{}', js: '', controls: null, zaps: [], updatedAt: 1 }

  it('previews CSS 300 ms after the last keystroke, saves on Cmd+S and closes on Esc', async () => {
    const { stub, push } = mount(snapshot({ enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 0 }), { 'tweaks.get': () => saved, 'tweaks.save': () => null, 'tweaks.previewCss': () => null })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    fireEvent.click(screen.getByRole('tab', { name: 'CSS' }))
    const area = await screen.findByDisplayValue('h1{}')
    vi.useFakeTimers()
    fireEvent.change(area, { target: { value: 'h1{color:red}' } })
    fireEvent.change(area, { target: { value: 'h1{color:blue}' } })
    act(() => vi.advanceTimersByTime(PREVIEW_MS - 1))
    expect(stub.calls.filter((c) => c.name === 'tweaks.previewCss')).toEqual([])
    act(() => vi.advanceTimersByTime(1))
    expect(stub.calls.filter((c) => c.name === 'tweaks.previewCss')).toEqual([{ name: 'tweaks.previewCss', payload: { tabId: TAB, css: 'h1{color:blue}' } }])
    vi.useRealTimers()
    fireEvent.keyDown(area, { key: 's', metaKey: true })
    await waitFor(() => expect(stub.calls).toContainEqual({ name: 'tweaks.save', payload: { tabId: TAB, css: 'h1{color:blue}', js: '', controls: DEFAULT_TWEAK_CONTROLS } }))
    fireEvent.keyDown(area, { key: 'Escape' })
    expect(stub.calls).toContainEqual({ name: 'overlay.close', payload: { instanceId: INST } })
  })

  it('docks at its rect over the page card in the top layer', () => {
    mount(snapshot({ enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 0 }), { 'tweaks.get': () => saved })
    render(<TweakEditor instance={{ ...instance, layer: 'top', rect: { x: 880, y: 12, width: 380, height: 776 } }} windowId={TAB} />)
    const editor = screen.getByRole('dialog')
    expect(editor.style.position).toBe('fixed')
    expect([editor.style.left, editor.style.top, editor.style.width, editor.style.height]).toEqual(['880px', '12px', '380px', '776px'])
  })

  // docs/chromium/fixes.md: the browser shrinks the top overlay to the editor so the page beside it takes clicks; the
  // editor then fills that smaller document instead of drawing off its edge.
  it('draws from the top overlay\'s own origin while it covers only the editor', () => {
    const WIN = '00000000-0000-4000-8000-000000000009'
    const state = snapshot({ enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 0 })
    const win = defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: WIN })
    const { push } = mount({ ...state, windows: { [WIN]: { ...win, overlay: { surfaces: [], topRect: { x: 880, y: 12, width: 380, height: 776 } } } } }, { 'tweaks.get': () => saved })
    render(<TweakEditor instance={{ ...instance, layer: 'top', rect: { x: 880, y: 12, width: 380, height: 776 } }} windowId={WIN} />)
    push()
    const editor = screen.getByRole('dialog')
    expect([editor.style.left, editor.style.top, editor.style.width, editor.style.height]).toEqual(['0px', '0px', '380px', '776px'])
  })

  it('shows no tweak as off and disabled', async () => {
    const { push } = mount(snapshot(null), { 'tweaks.get': () => null })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    const sw = screen.getByRole('switch', { name: 'Site tweaks on' })
    expect(sw).toBeDisabled()
    expect(sw).toHaveAttribute('aria-checked', 'false')
  })

  it('shows an empty tweak Site controls turned on as on, like Site controls', async () => {
    const { push } = mount(snapshot({ enabled: true, hasCss: false, hasJs: false, hasControls: false, zapCount: 0 }), { 'tweaks.get': () => null })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    const sw = screen.getByRole('switch', { name: 'Site tweaks on' })
    expect(sw).toBeEnabled()
    expect(sw).toHaveAttribute('aria-checked', 'true')
  })

  it('refuses code over 256 KB with an inline error and never sends it', async () => {
    const { stub, push } = mount(snapshot(null), { 'tweaks.get': () => null })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    fireEvent.click(screen.getByRole('tab', { name: 'CSS' }))
    await waitFor(() => expect(screen.getByRole('textbox', { name: 'CSS' })).toBeEnabled())
    fireEvent.click(screen.getByRole('tab', { name: 'JS' }))
    fireEvent.change(screen.getByRole('textbox', { name: 'JavaScript' }), { target: { value: 'x'.repeat(MAX_CODE_BYTES + 1) } })
    expect(screen.getByRole('alert')).toHaveTextContent('JavaScript is larger than 256 KB.')
    fireEvent.click(screen.getByRole('button', { name: /Save/ }))
    expect(stub.calls.some((c) => c.name === 'tweaks.save')).toBe(false)
  })

  it('lists hidden elements with a restore button each, and starts zap mode from the Hidden tab', async () => {
    const zaps = [
      { selector: '#promo', label: 'div · Promo banner', related: false, createdAt: 1 },
      { selector: 'div.card', label: '', related: true, createdAt: 2 }
    ]
    const { stub, push } = mount(snapshot({ enabled: true, hasCss: true, hasJs: false, hasControls: false, zapCount: 2 }), { 'tweaks.get': () => ({ ...saved, zaps }), 'tweaks.removeZap': () => null })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    fireEvent.click(await screen.findByRole('tab', { name: 'Hidden (2)' }))
    const list = await screen.findByRole('list', { name: 'Hidden elements' })
    expect(list).toHaveTextContent('div · Promo banner#promo')
    expect(list).toHaveTextContent('Elementdiv.card')
    fireEvent.click(screen.getByRole('button', { name: 'Restore div.card' }))
    expect(stub.calls).toContainEqual({ name: 'tweaks.removeZap', payload: { tabId: TAB, index: 1 } })
    fireEvent.click(screen.getByRole('button', { name: 'Zap elements' }))
    expect(stub.calls).toContainEqual({ name: 'tweaks.startZap', payload: { tabId: TAB } })
  })

  it('opens on the Style tab, previews each control change at once and saves the controls', async () => {
    const stored = { ...DEFAULT_TWEAK_CONTROLS, contrast: 1.5 }
    const { stub, push } = mount(snapshot({ enabled: true, hasCss: true, hasJs: false, hasControls: true, zapCount: 0 }), {
      'tweaks.get': () => ({ ...saved, controls: stored }),
      'tweaks.save': () => null,
      'tweaks.previewControls': () => null
    })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    expect(screen.getByRole('tab', { name: 'Style' })).toHaveAttribute('aria-selected', 'true')
    const hue = await screen.findByRole('slider', { name: 'Hue' })
    await waitFor(() => expect(hue).toHaveFocus())
    expect(screen.getByRole('slider', { name: 'Contrast' })).toHaveAttribute('aria-valuetext', '150%')
    expect(screen.getByRole('button', { name: 'Reset colors' })).toBeEnabled()
    expect(screen.getByRole('button', { name: 'Reset text' })).toBeDisabled()
    // Every slider has a label and a value text.
    for (const s of screen.getAllByRole('slider')) expect(s).toHaveAttribute('aria-valuetext')
    expect(screen.getAllByRole<HTMLInputElement>('slider').map((s) => s.labels?.[0]?.textContent)).toEqual(['Hue', 'Contrast', 'Brightness', 'Saturation', 'Text size'])

    fireEvent.change(hue, { target: { value: '45' } })
    expect(hue).toHaveAttribute('aria-valuetext', '+45°')
    expect(stub.calls.at(-1)).toEqual({ name: 'tweaks.previewControls', payload: { tabId: TAB, controls: { ...stored, hue: 45 } } })
    fireEvent.click(screen.getByRole('switch', { name: 'Invert colors' }))
    fireEvent.change(screen.getByRole('combobox', { name: 'Font' }), { target: { value: 'Georgia' } })
    fireEvent.change(screen.getByRole('slider', { name: 'Text size' }), { target: { value: '1.25' } })
    const next = { ...stored, hue: 45, invert: true, fontFamily: 'Georgia', fontScale: 1.25 }
    expect(stub.calls.at(-1)).toEqual({ name: 'tweaks.previewControls', payload: { tabId: TAB, controls: next } })
    expect(stub.calls.some((c) => c.name === 'tweaks.save')).toBe(false)
    expect(screen.getByRole('status')).toHaveTextContent('Unsaved changes')

    fireEvent.click(screen.getByRole('button', { name: /Save/ }))
    await waitFor(() => expect(stub.calls).toContainEqual({ name: 'tweaks.save', payload: { tabId: TAB, css: 'h1{}', js: '', controls: next } }))
    await waitFor(() => expect(screen.getByRole('status')).toHaveTextContent('Saved'))

    fireEvent.click(screen.getByRole('button', { name: 'Reset colors' }))
    expect(stub.calls.at(-1)).toEqual({ name: 'tweaks.previewControls', payload: { tabId: TAB, controls: { ...DEFAULT_TWEAK_CONTROLS, fontFamily: 'Georgia', fontScale: 1.25 } } })
    fireEvent.change(screen.getByRole('combobox', { name: 'Font' }), { target: { value: '' } })
    expect(stub.calls.at(-1)).toMatchObject({ payload: { controls: { fontFamily: null } } })
  })

  it('reads missing controls as the defaults', async () => {
    const { push } = mount(snapshot(null), { 'tweaks.get': () => null })
    render(<TweakEditor instance={instance} windowId={TAB} />)
    push()
    expect(await screen.findByRole('slider', { name: 'Hue' })).toHaveValue('0')
    expect(screen.getByRole('switch', { name: 'Invert colors' })).toHaveAttribute('aria-checked', 'false')
    expect(screen.getByRole('combobox', { name: 'Font' })).toHaveValue('')
    expect(hueText(-30)).toBe('−30°')
  })
})
