// Site controls popover (PLAN decision 8). An arrow at the
// sliders button, four square page actions (Share, Site tweaks, Screenshot, Capture text), "Extensions" (each action's
// icon and badge, then "+" for the store), "Settings" (one row per site permission with a decision), then the
// connection pill and a round "…" button. Its menu:
// Clear Cache, Clear Cookies, Manage Extensions…, Add Extension…, All Site Settings…, which replaces the panel's content
// with Rio's own controls (blocking, Site tweaks, every permission, zoom) and a back button. An internal page has no site, so only zoom applies to it.
import { useCallback, useEffect, useRef, useState, type MouseEvent, type ReactNode } from 'react'
import { permissionLabel, type PermissionRow } from '../../../shared/capabilities'
import { LOOK } from '../../../shared/look/tokens'
import type { OverlaySurfaceState } from '../../../shared/state'
import type { ActionButton } from '../../../shared/state/extensions'
import { siteControlsPage } from '../../../shared/state/extensions'
import { invoke } from '../../common/invoke'
import { actionLabel } from '../../ui/extensions/ActionRow'
import { IconButton } from '../../common/IconButton'
import { STORE_URL } from './ExtensionsMenu'
import { MenuList } from '../../common/MenuList'
import type { MenuItem } from '../../../shared/types'
import { useCluState } from '../../common/useCluState'
import { useReducedMotion } from '../../common/useReducedMotion'
import { SitePermissionsPanel } from '../capabilities/SitePermissionsPanel'
import { closeInstance, type SurfaceProps } from '../core/types'
import { ZoomControls } from '../engine/ZoomControls'
import { TweaksSection } from '../tweaks/TweaksSection'
import './siteControls.css'
import { clu } from '../../common/clu'
import { Toggle } from '../../common/controls'

const WIDTH = 236
const MARGIN = 8
const GAP = 10
const ARROW_INSET = 35
const NONE: ActionButton[] = []
const NO_SURFACES: OverlaySurfaceState[] = []
/** Dismissing (Esc, a click outside) plays the 120 ms entry reversed before the panel closes. */
export const SITE_CONTROLS_CLOSE_MS = LOOK.motion.spaceTintMs

/** Under the button with its arrow on the button's center, kept inside the window. */
export function popoverPosition(anchor: { x: number; y: number; width: number; height: number }, viewport: { width: number; height: number }) {
  const top = anchor.y + anchor.height + GAP
  const center = anchor.x + anchor.width / 2
  const left = Math.max(MARGIN, Math.min(center - WIDTH / 2, viewport.width - WIDTH - MARGIN))
  // The 27 wide arrow stays clear of the 21 pt corners.
  const arrow = Math.max(ARROW_INSET, Math.min(center - left, WIDTH - ARROW_INSET))
  return { left, top, maxHeight: Math.max(0, viewport.height - top - MARGIN), arrow }
}

const rectOf = (e: MouseEvent<HTMLElement>) => {
  const r = e.currentTarget.getBoundingClientRect()
  return { x: r.x, y: r.y, width: r.width, height: r.height }
}

const Glyph = ({ d }: { d: string }) => (
  <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d={d} />
  </svg>
)
const SHARE = 'M9 2.5v8.5M6 5.5l3-3 3 3M5.5 8H4.5v7.5h9V8h-1'
const CAMERA = 'M2.5 6h3l1.5-2h4l1.5 2h3v8.5h-13zM9 7.75a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5'
/** Capture text: a scan frame's four corners around two lines of text. */
const SCAN = 'M2.5 6V3.5a1 1 0 0 1 1-1H6M12 2.5h2.5a1 1 0 0 1 1 1V6M15.5 12v2.5a1 1 0 0 1-1 1H12M6 15.5H3.5a1 1 0 0 1-1-1V12M6 7.25h6M6 10.75h4'
const BRUSH = 'M10.5 7.5l-5 5.5a1.6 1.6 0 0 0 2.3 2.2l5-5.5M10.5 7.5l2 2M12 5.5l2 2M3 3.5h3M4.5 2v3M13.5 1.5v2M12.5 2.5h2'
const BACK = 'M11 3.5 5.5 9l5.5 5.5'
const OPEN_LOCK = 'M5 8V6a4 4 0 0 1 7.5-2M4 8h10v7.5H4z'
/** The lock is solid: a stroked shackle over a filled body. */
const SolidLock = () => (
  <svg width="18" height="18" viewBox="0 0 18 18" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" data-lock="solid">
    <path d="M5.75 8V6a3.25 3.25 0 0 1 6.5 0v2" />
    <rect x="4" y="8" width="10" height="7.5" rx="1.5" fill="currentColor" strokeWidth="0" />
  </svg>
)
const GEAR = 'M9 6.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5M9 1.5v2M9 14.5v2M1.5 9h2M14.5 9h2M3.7 3.7l1.4 1.4M12.9 12.9l1.4 1.4M3.7 14.3l1.4-1.4M12.9 5.1l1.4-1.4'
/** Developer Mode's hammer. */
const HAMMER = 'M10 4.5 4 10.5a1.4 1.4 0 0 0 2 2l6-6M8.5 3l2-1.2 4.2 4.2-1.2 2-1.4-1.4-2.2.4-.4-2.2z'
/** The "…" menu. */
export const MORE_MENU: MenuItem[] = [
  { kind: 'item', id: 'page.clearCacheAndRefresh', label: 'Clear Cache' },
  { kind: 'item', id: 'page.clearCookiesAndRefresh', label: 'Clear Cookies' },
  { kind: 'separator' },
  { kind: 'item', id: 'extensions.open', label: 'Manage Extensions…' },
  { kind: 'item', id: 'store', label: 'Add Extension…' },
  { kind: 'separator' },
  { kind: 'item', id: 'all', label: 'All Site Settings…' }
]
const MENU_HEIGHT = 200

function webOrigin(url: string | undefined): string | null {
  try {
    const u = new URL(url ?? '')
    return u.protocol === 'http:' || u.protocol === 'https:' ? u.origin : null
  } catch {
    return null
  }
}

/** The site's permissions with a decision, the ones that differ from the default (Ask). */
function useSiteSettings(tabId: string, url: string | undefined): PermissionRow[] {
  const profileId = useCluState((s) => s.tabs[tabId]?.profileId ?? '')
  const origin = webOrigin(url)
  const [rows, setRows] = useState<PermissionRow[]>([])
  const load = useCallback(async () => {
    if (!origin || !profileId) return setRows([])
    const r = await invoke(clu().permission.list({ profileId, origin }))
    setRows(r.ok ? r.value.filter((row) => row.decision === 'allow' || row.decision === 'deny') : [])
  }, [origin, profileId])
  useEffect(() => {
    void load()
  }, [load])
  return rows
}

const Slot = ({ name, children }: { name: string; children: ReactNode }) => (
  <div className="clu-site-controls-slot" data-slot={name}>
    {children}
  </div>
)

export function SiteControlsPopover({ instance, windowId }: SurfaceProps) {
  const tabId = useCluState((s) => siteControlsPage(s, windowId)?.id ?? null)
  const url = useCluState((s) => siteControlsPage(s, windowId)?.url)
  const site = useCluState((s) => siteControlsPage(s, windowId)?.site ?? null)
  const count = useCluState((s) => Math.max(0, siteControlsPage(s, windowId)?.blockedCount ?? 0))
  const enabled = useCluState((s) => siteControlsPage(s, windowId)?.blockingEnabled ?? true)
  const devMode = useCluState((s) => siteControlsPage(s, windowId)?.devMode ?? false)
  const security = useCluState((s) => (tabId ? s.tabs[tabId]?.security : undefined) ?? (url?.startsWith('https:') ? 'secure' : 'none'))
  const actions = useCluState((s) => s.windows[windowId]?.actionRow?.actions ?? NONE)
  const settings = useSiteSettings(tabId ?? '', url)
  const [more, setMore] = useState(false)
  // Where the "…" menu hangs, in the panel's own coordinates: under the button, or over it near the window's bottom.
  const [menuAt, setMenuAt] = useState<{ right: number; top?: number; bottom?: number } | null>(null)
  const moreRect = useRef({ x: 0, y: 0, width: 0, height: 0 })
  const ref = useRef<HTMLDivElement>(null)
  const close = () => closeInstance(instance.instanceId)
  const run = (id: string) => {
    close()
    void invoke(clu().commands.run({ id }))
  }
  const gone = !tabId

  useEffect(() => {
    if (gone) closeInstance(instance.instanceId)
  }, [gone, instance.instanceId])

  const [closing, setClosing] = useState(false)
  const reduced = useReducedMotion()
  // Only the top light-dismiss surface takes the outside click; a menu opened over the panel closes first.
  const top = useCluState((s) => (s.windows[windowId]?.overlay.surfaces ?? NO_SURFACES).filter((i) => i.layer === instance.layer && i.modality === 'light-dismiss').at(-1)?.instanceId === instance.instanceId)
  useEffect(() => {
    if (!closing) return
    const timer = setTimeout(() => closeInstance(instance.instanceId), reduced ? 0 : SITE_CONTROLS_CLOSE_MS)
    return () => clearTimeout(timer)
  }, [closing, reduced, instance.instanceId])

  useEffect(() => {
    ref.current?.focus()
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return
      e.preventDefault()
      setClosing(true)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [instance.instanceId])

  useEffect(() => {
    if (!top) return
    // Captured before the overlay root's light dismiss, which would close the panel in one frame.
    const onDown = (e: PointerEvent) => {
      if (e.target instanceof Node && ref.current?.contains(e.target)) return
      e.stopPropagation()
      setClosing(true)
    }
    window.addEventListener('pointerdown', onDown, true)
    return () => window.removeEventListener('pointerdown', onDown, true)
  }, [top])

  if (!tabId) return null
  const { arrow, ...pos } = popoverPosition(instance.rect ?? { x: 0, y: 0, width: 0, height: 0 }, { width: window.innerWidth, height: window.innerHeight })
  const secure = security === 'secure'
  return (
    <div ref={ref} role="dialog" aria-label="Site controls" tabIndex={-1} className="clu-site-controls" data-closing={closing || undefined} style={{ ...pos, ['--arrow-x' as string]: `${arrow}px` }} onPointerDown={(e) => e.stopPropagation()}>
      {more ? (
        // All Site Settings replaces the popover's content in place, so the two never overlap.
        <div className="clu-site-controls-head">
          <IconButton aria-label="Back to site controls" className="clu-site-controls-back" onClick={() => setMore(false)}>
            <Glyph d={BACK} />
          </IconButton>
          <h2>All Site Settings</h2>
        </div>
      ) : (
        <>
          <div className="clu-site-controls-body">
            {site ? (
              <div className="clu-site-controls-top" role="toolbar" aria-label="Page actions">
                <IconButton
                  aria-label="Share"
                  className="clu-site-controls-tile"
                  data-site-share
                  onClick={(e) => {
                    const anchor = rectOf(e)
                    close()
                    void invoke(clu().siteControls.share({ anchor }))
                  }}
                >
                  <Glyph d={SHARE} />
                </IconButton>
                <IconButton aria-label="Site tweaks" className="clu-site-controls-tile" onClick={() => run('tweaks.open')}>
                  <Glyph d={BRUSH} />
                </IconButton>
                <IconButton
                  aria-label="Screenshot"
                  className="clu-site-controls-tile"
                  data-site-screenshot
                  onClick={() => {
                    close()
                    void invoke(clu().siteControls.screenshot())
                  }}
                >
                  <Glyph d={CAMERA} />
                </IconButton>
                <IconButton
                  aria-label="Capture text"
                  className="clu-site-controls-tile"
                  data-site-capture-text
                  onClick={() => {
                    close()
                    void invoke(clu().siteControls.captureText())
                  }}
                >
                  <Glyph d={SCAN} />
                </IconButton>
              </div>
            ) : null}
            <section className="clu-site-controls-section" data-slot="extensions" aria-label="Extensions">
              <h2>Extensions</h2>
              <div className="clu-site-controls-tiles" data-action-row>
                {actions.map((a) => (
                  <IconButton
                    key={a.id}
                    aria-label={actionLabel(a)}
                    className="clu-site-controls-tile clu-action-button"
                    data-action={a.id}
                    data-badge={a.badge || undefined}
                    aria-disabled={!a.enabled || undefined}
                    onClick={(e) => void invoke(clu().extensions.runAction({ id: a.id, anchor: rectOf(e) }))}
                  >
                    {a.icon ? <img src={a.icon} alt="" width={14} height={14} draggable={false} /> : <span className="clu-action-letter">{a.name.slice(0, 1).toUpperCase()}</span>}
                    {a.badge ? <span className="clu-site-controls-badge" aria-hidden="true">{a.badge.slice(0, 4)}</span> : null}
                  </IconButton>
                ))}
                <IconButton
                  aria-label="Add extensions"
                  className="clu-site-controls-tile"
                  data-extensions-store
                  onClick={() => {
                    close()
                    void invoke(clu().tabs.create({ url: STORE_URL }))
                  }}
                >
                  <Glyph d="M9 3.5v11M3.5 9h11" />
                </IconButton>
              </div>
            </section>
            {settings.length > 0 || site ? (
              <section className="clu-site-controls-section" data-slot="settings" aria-label="Settings">
                <h2>Settings</h2>
                <ul className="clu-site-controls-settings">
                  {settings.map((r) => (
                    <li key={r.permission}>
                      <button type="button" className="clu-site-controls-setting" data-permission={r.permission} data-decision={r.decision} onClick={() => setMore(true)}>
                        <span className="clu-site-controls-setting-icon" aria-hidden="true">
                          <Glyph d={GEAR} />
                        </span>
                        <span className="clu-site-controls-setting-text">
                          <strong>{permissionLabel(r.permission)}</strong>
                          <span>{r.decision === 'allow' ? 'Allowed' : 'Blocked'}</span>
                        </span>
                      </button>
                    </li>
                  ))}
                  {site ? (
                    <li>
                      <button
                        type="button"
                        className="clu-site-controls-setting"
                        data-dev-mode={devMode ? 'on' : 'off'}
                        aria-pressed={devMode}
                        onClick={() => void invoke(clu().siteControls.setDevMode({ tabId, on: !devMode }))}
                      >
                        <span className="clu-site-controls-setting-icon" aria-hidden="true">
                          <Glyph d={HAMMER} />
                        </span>
                        <span className="clu-site-controls-setting-text">
                          <strong>Developer Mode</strong>
                          <span>{devMode ? 'On' : 'Off'}</span>
                        </span>
                      </button>
                    </li>
                  ) : null}
                </ul>
              </section>
            ) : null}
          </div>
          <div className="clu-site-controls-footer">
            <span className="clu-site-controls-security" data-security={security}>
              {secure ? <SolidLock /> : <Glyph d={OPEN_LOCK} />}
              {secure ? 'Secure' : site ? 'Not secure' : 'Rio page'}
            </span>
            <IconButton
              aria-label="More site controls"
              aria-haspopup="menu"
              aria-expanded={!!menuAt}
              className="clu-site-controls-more"
              data-site-controls-more
              onClick={(e) => {
                if (menuAt) return setMenuAt(null)
                moreRect.current = rectOf(e)
                const b = e.currentTarget.getBoundingClientRect()
                const p = ref.current?.getBoundingClientRect() ?? b
                const right = p.right - b.right
                setMenuAt(b.bottom + 4 + MENU_HEIGHT > window.innerHeight ? { right, bottom: p.bottom - b.top + 4 } : { right, top: b.bottom - p.top + 4 })
              }}
            >
              <svg width="16" height="16" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">
                <circle cx="3" cy="8" r="1.4" />
                <circle cx="8" cy="8" r="1.4" />
                <circle cx="13" cy="8" r="1.4" />
              </svg>
            </IconButton>
          </div>
          {menuAt ? (
            <MenuList
              label="More site controls"
              items={MORE_MENU.map((it) => (it.kind === 'item' && it.id.startsWith('page.') && !site ? { ...it, enabled: false } : it))}
              style={{ position: 'absolute', zIndex: 1, ...menuAt }}
              onClose={() => setMenuAt(null)}
              onPick={(id) => {
                setMenuAt(null)
                if (id === 'all') return setMore(true)
                // Rio's extensions menu (pin, options, remove, and Chromium's page) hangs from the "…" button.
                if (id === 'extensions.open') return void invoke(clu().extensions.openMenu({ anchor: moreRect.current }))
                if (id === 'store') {
                  close()
                  return void invoke(clu().tabs.create({ url: STORE_URL }))
                }
                run(id)
              }}
            />
          ) : null}
        </>
      )}
      {more ? (
        <div className="clu-site-controls-more-panel" data-more>
          {site ? (
            <section className="clu-site-controls-blocking" aria-label="Blocking">
              <div className="clu-site-controls-row">
                <span aria-hidden="true">Block ads and trackers</span>
                <Toggle label="Block ads and trackers" checked={enabled} onChange={(on) => void invoke(clu().blocking.setSiteEnabled({ tabId, enabled: on }))} />
              </div>
              <p data-blocking-status>{enabled ? `${count} blocked on this page` : `Off for ${site}`}</p>
            </section>
          ) : null}
          {site ? (
            <Slot name="tweaks">
              <TweaksSection tabId={tabId} />
            </Slot>
          ) : null}
          {site ? (
            <Slot name="permissions">
              <SitePermissionsPanel tabId={tabId} />
            </Slot>
          ) : null}
          <Slot name="zoom">
            <ZoomControls tabId={tabId} />
          </Slot>
        </div>
      ) : null}
    </div>
  )
}
