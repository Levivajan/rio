// ui:fake harness page: stands in for a Chromium window. It owns the FakeHost both WebUIs connect to (boot-fake.ts),
// stacks the overlay view over the frame view as BrowserViewCluLayoutImpl will, and hands the overlay pointer and
// focus only while it shows a top-layer surface, as the browser shows and hides the overlay widget.
import '../../common/bridge/zod-jitless'
import { FAKE } from '../../common/bridge/fake-seed'
import { createFakeHost } from '../../common/bridge/fake'
import type { UpdateStatus } from '../../../shared/state/platform'
import './harness.css'

// `?history=10000` gives the command bar a large history index (the keystroke budget spec); `?downloads=1` seeds the
// profile's downloads (the sidebar's downloads button); `?onboarding=1` starts as a fresh install with first-run
// onboarding showing; `?extensions=1` seeds the action row and the blocker's fields (the M7 spec); `?update=ready` (or
// idle, checking, ...) starts the fake updater in that status (the update spec).
const query = new URLSearchParams(window.location.search)
const historyRows = Math.min(Math.max(Number(query.get('history')) || 0, 0), 20_000)
const UPDATE_STATUSES: readonly UpdateStatus[] = ['unavailable', 'idle', 'checking', 'downloading', 'ready', 'up-to-date', 'error']
const update = UPDATE_STATUSES.find((s) => s === query.get('update'))
const host = createFakeHost(undefined, {
  historyRows,
  downloads: query.get('downloads') === '1',
  onboarding: query.get('onboarding') === '1',
  extensions: query.get('extensions') === '1',
  ...(update ? { update } : {}),
  noArc: query.get('arc') === '0',
  keychainDenied: query.get('keychain') === 'deny',
})
window.cluFakeHost = host

const frame = document.getElementById('frame') as HTMLIFrameElement
const overlay = document.getElementById('overlay') as HTMLIFrameElement
const underPeek = document.getElementById('under-peek') as HTMLIFrameElement

const resize = () => host.setContentSize({ width: window.innerWidth, height: window.innerHeight })
window.addEventListener('resize', resize)
resize()

// Passive surfaces (toasts, the Ctrl+Tab switcher, hover cards, the sidebar's hover peek) each get a view of their own
// at the surface's rect, as BrowserViewCluLayoutImpl shows a passive widget per slot.
const passive = new Map<number, HTMLIFrameElement>()
function placePassive(surfaces: readonly { layer: string; slot: number | null; rect: { x: number; y: number; width: number; height: number } | null }[]): void {
  const live = new Set<number>()
  for (const s of surfaces) {
    if (s.layer !== 'passive' || s.slot === null || !s.rect) continue
    live.add(s.slot)
    let view = passive.get(s.slot)
    if (!view) {
      view = document.createElement('iframe')
      view.className = 'passive'
      view.title = 'Rio passive overlay'
      view.src = `./overlay/index.html?layer=passive&slot=${s.slot}`
      view.dataset.slot = String(s.slot)
      document.getElementById('window')?.append(view)
      passive.set(s.slot, view)
    }
    Object.assign(view.style, { left: `${s.rect.x}px`, top: `${s.rect.y}px`, width: `${s.rect.width}px`, height: `${s.rect.height}px` })
  }
  for (const [slot, view] of passive) {
    if (live.has(slot)) continue
    view.remove()
    passive.delete(slot)
  }
}

let wasOpen = false
host.subscribe((s) => {
  // Chromium paints an opaque backdrop behind an iframe whose color-scheme differs from its embedder's, which would
  // hide the frame under a dark overlay. The browser stacks real views instead, so the harness keeps the schemes equal.
  const scheme = s.app.appearance.resolved
  for (const el of [document.documentElement, frame, overlay, underPeek]) el.style.colorScheme = scheme
  const surfaces = s.windows[FAKE.window]?.overlay.surfaces ?? []
  placePassive(surfaces)
  underPeek.dataset.open = String(surfaces.some((i) => i.layer === 'under-peek'))
  for (const view of passive.values()) view.style.colorScheme = scheme
  const open = surfaces.some((i) => i.layer === 'top')
  overlay.dataset.open = String(open)
  if (open && !wasOpen && surfaces.some((i) => i.layer === 'top' && i.focus)) overlay.focus()
  if (!open && wasOpen) frame.focus()
  wasOpen = open
})
