// WebUI bridge boot for the ui:fake harness (`@clu/webui-boot` with --fake): install a FakeBridge. Inside the harness
// page both surfaces share its FakeHost, so the overlay sees what the frame does; opened alone, a page gets its own.
// The fake also plays the browser's part for keys: table chords run commands.run as Chromium's accelerators will.
import '../common/bridge/zod-jitless'
import { chordKey, parseChord } from '../../shared/commands'
import { customShortcuts, effectiveTable, keysOf, SHORTCUTS_KEY } from '../../shared/shortcuts'
import { installBridge } from '../common/clu'
import { createFakeHost, type FakeHost } from '../common/bridge/fake'
import type { WebUiSurface } from './boot-mojo'

declare global {
  interface Window {
    cluFakeHost?: FakeHost
  }
}

const LAYERS = ['top', 'under-peek', 'passive'] as const

function hostFor(): FakeHost {
  try {
    const parent = window.parent !== window ? window.parent.cluFakeHost : undefined
    if (parent) return parent
  } catch {
    // A cross-origin parent: fall through to this window's own host.
  }
  window.cluFakeHost ??= createFakeHost()
  return window.cluFakeHost
}

export async function bootBridge(surface: WebUiSurface): Promise<void> {
  const host = hostFor()
  const q = new URLSearchParams(window.location.search)
  const layer = LAYERS.find((l) => l === q.get('layer')) ?? 'top'
  // A passive view shows the one surface of its slot (the harness opens one per passive surface, as the browser does).
  const slotParam = Number(q.get('slot'))
  const slot = layer === 'passive' && Number.isInteger(slotParam) && slotParam >= 0 ? slotParam : null
  const bridge =
    surface === 'frame'
      ? host.connect({ surface: 'ui' })
      : surface === 'settings'
        ? host.connect({ surface: 'internal:settings' })
        : host.connect({ surface: 'overlay', layer, slot })
  installBridge(bridge)
  // Playwright drives the harness through window.clu.
  window.clu = bridge

  // The settings window has no key table (its Shortcuts tab records keys); a Rio window's WebUIs do.
  if (surface === 'settings') return
  // The key table with the user's keys (Settings, Shortcuts), rebuilt when shortcuts.custom changes.
  let chords = new Map<string, string>()
  let built: unknown = null
  const table = () => {
    const custom = host.state().settings.global[SHORTCUTS_KEY]
    if (custom === built && chords.size) return chords
    built = custom
    chords = new Map()
    for (const e of effectiveTable(customShortcuts(custom))) for (const keys of keysOf(e)) chords.set(chordKey(parseChord(keys)), e.id)
    return chords
  }
  window.addEventListener(
    'keydown',
    (e) => {
      const id = table().get(chordKey({ code: e.code, meta: e.metaKey, ctrl: e.ctrlKey, shift: e.shiftKey, alt: e.altKey }))
      if (!id) return
      e.preventDefault()
      void bridge.commands.run({ id })
    },
    true
  )
}
