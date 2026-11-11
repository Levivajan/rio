// FakeBridge handlers and seed for M7 (docs/chromium/m7.md): the action row and extensions menu, Site controls,
// the blocker's switch and zap mode, answered as CluWindowSite and the M7 handlers do, enough for the harness and its
// screenshots. The seed is opt-in (`?extensions=1` in the harness), so other specs keep the plain sidebar.
import { KNOWN_PERMISSIONS, type PermissionRow } from '../../../shared/capabilities'
import type { CluState, OverlaySurfaceState } from '../../../shared/state'
import type { ActionButton } from '../../../shared/state/extensions'
import type { SiteTweakRow } from '../../../shared/tweaks'
import type { Rect } from '../../../shared/types'
import { FAKE } from './fake-seed'

const NOTES = 'a'.repeat(32)
const VAULT = 'b'.repeat(32)
const PANEL = 'c'.repeat(32)

// 16x16 PNGs, one color each, so the row shows three distinct icons.
const icon = (b64: string) => `data:image/png;base64,${b64}`
const ICONS = {
  notes: icon('iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAGUlEQVR4nGOwbvr2nxLMMGrAqAGjBgwXAwCGGLIfFYFKqwAAAABJRU5ErkJggg=='),
  vault: icon('iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAGUlEQVR4nGMQ2Nn4nxLMMGrAqAGjBgwXAwBHHUkf1AtOXwAAAABJRU5ErkJggg=='),
  panel: icon('iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAGUlEQVR4nGP4Yh/3nxLMMGrAqAGjBgwXAwCyHJAfNugj3QAAAABJRU5ErkJggg==')
}

const ACTIONS: ActionButton[] = [
  { id: NOTES, name: 'Notes', title: 'Notes', icon: ICONS.notes, badge: '3', enabled: true, pinned: true },
  { id: VAULT, name: 'Vault', title: 'Vault', icon: ICONS.vault, badge: '', enabled: true, pinned: true },
  { id: PANEL, name: 'Research panel', title: 'Research panel', icon: ICONS.panel, badge: '', enabled: true, pinned: false }
]

/** The seed with three extensions in the window's profile and blocker fields on the window's tabs. */
export function withSiteSeed(state: CluState): CluState {
  const win = state.windows[FAKE.window]
  if (!win) return state
  const profileId = state.spaces[win.activeSpaceId]?.profileId ?? Object.keys(state.profiles)[0] ?? ''
  const tabs = Object.fromEntries(
    Object.entries(state.tabs).map(([id, t]) => {
      const site = /^https?:\/\/(?:www\.)?([^/:]+)/.exec(t.url)?.[1]?.toLowerCase() ?? null
      return [id, { ...t, site, blockedCount: site ? 12 : 0, blockingEnabled: true, devMode: site === 'localhost' || site === '127.0.0.1' }]
    })
  )
  return {
    ...state,
    tabs,
    windows: { ...state.windows, [FAKE.window]: { ...win, actionRow: { profileId, actions: ACTIONS } } }
  }
}

export interface SiteHost {
  state(): CluState
  set(path: string[], value: unknown): void
  openSurface(windowId: string, spec: Omit<OverlaySurfaceState, 'instanceId' | 'slot' | 'openedFrom' | 'layer'>): string
  /** Lays every window out again (the Developer Mode strip's band follows the front page). */
  relayoutAll(): void
}

type Ctx = { windowId: string }
const popover = (surface: string, rect: Rect): Omit<OverlaySurfaceState, 'instanceId' | 'slot' | 'openedFrom' | 'layer'> => ({
  surface,
  modality: 'light-dismiss',
  rect,
  focus: true,
  props: {}
})

// The http or https origin of `url` ("https://a.example"), as clu_site_permissions.cc reads it; null otherwise.
function webOrigin(url: string): string | null {
  try {
    const u = new URL(url)
    return (u.protocol === 'http:' || u.protocol === 'https:') && u.hostname ? u.origin : null
  } catch {
    return null
  }
}

// Site permissions as CluPermissionHandler answers them over Chromium's site settings (docs/chromium/fixes.md), kept in
// memory: two sites that are no seeded tab already have decisions, so Rio's settings list something.
function permissionStore() {
  const rows: PermissionRow[] = [
    { profileId: FAKE.profile, origin: 'https://meet.example.com', permission: 'camera', decision: 'allow', updatedAt: 1_762_000_000_000 },
    { profileId: FAKE.profile, origin: 'https://meet.example.com', permission: 'microphone', decision: 'allow', updatedAt: 1_762_000_000_000 },
    { profileId: FAKE.profile, origin: 'https://alerts.example.org', permission: 'notifications', decision: 'deny', updatedAt: 1_762_100_000_000 },
    { profileId: FAKE.profile, origin: 'https://alerts.example.org', permission: 'sound', decision: 'deny', updatedAt: 1_762_100_000_000 }
  ]
  const order = (r: PermissionRow) => KNOWN_PERMISSIONS.indexOf(r.permission)
  const bad = (message: string) => ({ __fakeError: { code: 'bad-payload', message } })
  return {
    'permission.list': (p: { profileId: string; origin?: string }) => {
      const origin = p.origin === undefined ? null : webOrigin(p.origin)
      if (p.origin !== undefined && !origin) return []
      return rows.filter((r) => r.profileId === p.profileId && (!origin || r.origin === origin)).sort((a, b) => order(a) - order(b))
    },
    'permission.set': (p: { profileId: string; origin: string; permission: string; decision: 'ask' | 'allow' | 'block' }) => {
      const origin = webOrigin(p.origin)
      if (!origin) return bad('not a web origin')
      if (!KNOWN_PERMISSIONS.includes(p.permission)) return bad('unknown permission')
      const at = rows.findIndex((r) => r.profileId === p.profileId && r.origin === origin && r.permission === p.permission)
      if (at >= 0) rows.splice(at, 1)
      if (p.decision !== 'ask') rows.push({ profileId: p.profileId, origin, permission: p.permission, decision: p.decision === 'allow' ? 'allow' : 'deny', updatedAt: Date.now() })
      return null
    },
    'permission.clear': (p: { profileId: string; origin?: string; permission?: string }) => {
      const origin = p.origin === undefined ? null : webOrigin(p.origin)
      if (p.origin !== undefined && !origin) return bad('not a web origin')
      if (p.permission !== undefined && !KNOWN_PERMISSIONS.includes(p.permission)) return bad('unknown permission')
      for (let i = rows.length - 1; i >= 0; i -= 1) {
        const r = rows[i]
        if (r && r.profileId === p.profileId && (!origin || r.origin === origin) && (p.permission === undefined || r.permission === p.permission)) rows.splice(i, 1)
      }
      return null
    }
  }
}

/** Saved site tweaks, answered to the settings page's Site tweaks list as CluTweakStore lists them. */
function tweakStore() {
  let rows: SiteTweakRow[] = [
    { site: 'news.example.com', enabled: true, hasCss: true, hasJs: false, hasControls: true, zapCount: 3, updatedAt: 1_762_560_000_000 },
    { site: 'docs.example.org', enabled: false, hasCss: true, hasJs: true, hasControls: false, zapCount: 0, updatedAt: 1_762_473_600_000 }
  ]
  return {
    'tweaks.list': () => rows,
    'tweaks.setEnabledForSite': (p: { site: string; enabled: boolean }) => {
      rows = rows.map((r) => (r.site === p.site ? { ...r, enabled: p.enabled } : r))
      return null
    },
    'tweaks.deleteForSite': (p: { site: string }) => {
      rows = rows.filter((r) => r.site !== p.site)
      return null
    }
  }
}

export function siteHandlers(host: SiteHost) {
  const actions = (windowId: string) => host.state().windows[windowId]?.actionRow?.actions ?? []
  const setActions = (windowId: string, next: ActionButton[]) => {
    const row = host.state().windows[windowId]?.actionRow
    if (row) host.set(['windows', windowId, 'actionRow'], { ...row, actions: next })
  }
  return {
    ...permissionStore(),
    ...tweakStore(),
    'siteControls.open': (p: { anchor: Rect }, { windowId }: Ctx) => {
      host.openSurface(windowId, popover('site-controls', p.anchor))
    },
    // The share picker is macOS's; the harness records the call.
    'siteControls.share': () => null,
    'siteControls.screenshot': () => null,
    // Developer Mode is per site: every tab on the site follows (CluWindowSite::SetDevMode).
    'siteControls.setDevMode': (p: { tabId: string; on: boolean }) => {
      const tab = host.state().tabs[p.tabId]
      if (!tab) return { __fakeError: { code: 'not-found' } }
      if (!tab.site) return { __fakeError: { code: 'rejected', reason: 'no-site' } }
      for (const t of Object.values(host.state().tabs)) if (t.site === tab.site) host.set(['tabs', t.id, 'devMode'], p.on)
      host.relayoutAll()
      return null
    },
    'siteControls.devTool': () => null,
    'siteControls.captureText': () => null,
    'extensions.openMenu': (p: { anchor: Rect }, { windowId }: Ctx) => {
      host.openSurface(windowId, popover('extensions-menu', p.anchor))
    },
    'extensions.setPinned': (p: { id: string; pinned: boolean }, { windowId }: Ctx) => {
      if (!actions(windowId).some((a) => a.id === p.id)) return { __fakeError: { code: 'not-found' } }
      // Chromium keeps the pins in order: a new pin goes last among them.
      const rest = actions(windowId).map((a) => (a.id === p.id ? { ...a, pinned: p.pinned } : a))
      setActions(windowId, [...rest.filter((a) => a.pinned && a.id !== p.id), ...rest.filter((a) => a.id === p.id && a.pinned), ...rest.filter((a) => !a.pinned)])
      return null
    },
    // The harness has no Web Store: every Reinstall lands.
    'extensions.installFromStore': () => null,
    'extensions.runAction': (p: { id: string }, { windowId }: Ctx) =>
      actions(windowId).some((a) => a.id === p.id) ? null : { __fakeError: { code: 'not-found' } },
    'blocking.setSiteEnabled': (p: { tabId: string; enabled: boolean }) => {
      const tab = host.state().tabs[p.tabId]
      if (!tab) return { __fakeError: { code: 'not-found' } }
      if (!tab.site) return { __fakeError: { code: 'rejected', reason: 'no-site' } }
      host.set(['tabs', p.tabId, 'blockingEnabled'], p.enabled)
      host.set(['tabs', p.tabId, 'blockedCount'], p.enabled ? 12 : 0)
      return null
    },
    'tweaks.get': () => null,
    // The panel's switch: a site without a tweak gets an empty one, on.
    'tweaks.setEnabled': (p: { tabId: string; enabled: boolean }) => {
      const tab = host.state().tabs[p.tabId]
      if (!tab?.site) return { __fakeError: { code: 'rejected', reason: 'no-site' } }
      if (!tab.tweak && !p.enabled) return null
      host.set(['tabs', p.tabId, 'tweak'], { hasCss: false, hasJs: false, hasControls: false, zapCount: 0, ...tab.tweak, enabled: p.enabled })
      return null
    },
    'tweaks.startZap': (p: { tabId: string }) => {
      if (!host.state().tabs[p.tabId]?.site) return { __fakeError: { code: 'rejected', reason: 'no-site' } }
      host.set(['tabs', p.tabId, 'zapMode'], { active: true, lastResult: null })
      return null
    },
    'tweaks.stopZap': (p: { tabId: string }) => {
      host.set(['tabs', p.tabId, 'zapMode'], null)
      return null
    }
  }
}
