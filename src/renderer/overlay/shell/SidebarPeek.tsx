// Hover peek surface (docs/plan/01-shell.md F4, PLAN 3.16 and decision 51): the scaffold Sidebar in
// mode "peek" (no action row, no drag) over the theme background, in a passive overlay view at the window's left
// edge. Leaving starts the 165 ms grace, then it slides out and asks main to close it; surfaces opened from it (row
// menu, hover card) hold it open until they close. While hovered it sends shell.pointerAlive every second. Pinned (the
// window shows no page) it floats in place with no slide and stays: leaving and Escape do nothing. Held by a Library
// opened in it, Escape closes the Library. Its top row
// carries the window's three buttons: the native ones sit under this overlay, so it draws its own at their
// docked place within the panel.
import { useEffect, useRef, useState, type CSSProperties } from 'react'
import { childrenOf, createPeekHover, peekMotion, PEEK_ALIVE_MS, PEEK_SLIDE_IN_MS, PEEK_SLIDE_OUT_DONE_MS, PEEK_SLIDE_OUT_EASE, PEEK_SLIDE_OUT_MS, type PeekHover } from '../../../shared/shell/peek'
import type { OverlaySurfaceState } from '../../../shared/state'
import { dragBus } from '../../common/dragBus'
import { useCluState } from '../../common/useCluState'
import { useReducedMotion } from '../../common/useReducedMotion'
import { Sidebar, SidebarWarmup } from '../../ui/sidebar/Sidebar'
import { useLibraryPeekHeld } from '../../ui/archive/LibraryPeek'
import { setRenaming, useAnyRenaming } from '../../ui/tabs/renaming'
import { useFloatingRenamePending } from '../../ui/tabs/sidebarRequest'
import { ThemeBackground } from '../../ui/theme/ThemeBackground'
import type { SurfaceProps } from '../core/types'
import '../../ui/styles.css'
import '../../ui/shell/shell.css'
import './peek.css'
import { clu } from '../../common/clu'
import { menuLight } from '../../common/menuChrome'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'
import { LOOK } from '../../../shared/look/tokens'

const NONE: OverlaySurfaceState[] = []
// A field keeps Escape while it has something to drop (the Library's empty search field, focused on open, does not).
const keepsEscape = (t: EventTarget | null) =>
  t instanceof HTMLElement &&
  (t.isContentEditable || t instanceof HTMLTextAreaElement || (t instanceof HTMLInputElement && (t.type !== 'search' || t.value !== '')))
// Every path that hides or releases the peek from here prints one "clu-peek" line: Chromium writes a WebUI's console
// to its log, so a run with --enable-logging=stderr --v=1 shows which one fired next to the browser's VLOG lines.
const trace = (what: string) => console.info(`clu-peek overlay: ${what}`)
const close = (reason: string) => {
  trace(`asks to close (${reason})`)
  void clu().shell.setSidebarPeek({ open: false })
}

const LIGHTS = LOOK.window.trafficLights
const LIGHT_STYLE = {
  left: LIGHTS.firstCenterX - LIGHTS.diameter / 2,
  top: LIGHTS.centerY - LIGHTS.diameter / 2,
  gap: LIGHTS.pitch - LIGHTS.diameter,
  '--light-d': `${LIGHTS.diameter}px`
} as CSSProperties

/** The window's close, minimize and full screen buttons, macOS's colors and hover glyphs; gray while the window is
 *  not focused. */
function WindowLights({ focused }: { focused: boolean }) {
  return (
    <div className="clu-peek-lights" data-shell="peek-lights" data-focused={String(focused)} style={LIGHT_STYLE}>
      <button type="button" data-light="close" aria-label="Close Window" onClick={() => void clu().window.close()}>
        <svg viewBox="0 0 12 12" aria-hidden="true">
          <path d="M3.5 3.5l5 5M8.5 3.5l-5 5" />
        </svg>
      </button>
      <button type="button" data-light="minimize" aria-label="Minimize" onClick={() => void clu().window.minimize()}>
        <svg viewBox="0 0 12 12" aria-hidden="true">
          <path d="M2.75 6h6.5" />
        </svg>
      </button>
      <button type="button" data-light="fullscreen" aria-label="Enter Full Screen" onClick={() => void clu().window.toggleFullscreen()}>
        <svg viewBox="0 0 12 12" aria-hidden="true">
          <path className="fill" d="M3.5 4.25v4.25h4.25zM8.5 7.75V3.5H4.25z" />
        </svg>
      </button>
    </div>
  )
}

/** The floating sidebar's view lays its pane out once while its widget is still hidden, so the first reveal after
 *  Cmd+S slides with no held frame. The overlay root renders it in the peek's passive view while no peek is open. */
export function SidebarPeekWarmup({ windowId }: { windowId: string }) {
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  const active = useCluState((s) => s.windows[windowId]?.activeSpaceId ?? '')
  if (!active) return null
  return (
    <div className="clu-peek-warm" style={{ width }}>
      <SidebarWarmup spaceIds={[active]} mode="peek" extra={<ThemeBackground />} />
    </div>
  )
}

export function SidebarPeek({ instance, windowId }: SurfaceProps) {
  const reduced = useReducedMotion()
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  const surfaces = useCluState((s) => s.windows[windowId]?.overlay.surfaces ?? NONE)
  const focused = useCluState((s) => s.windows[windowId]?.focused ?? true)
  const libraryOpen = useCluState((s) => s.archivePanel[windowId] === true)
  const popupHeld = useLibraryPeekHeld()
  // A row's rename field (Tabs ▸ New Folder… asked here) takes typing, so it holds the peek as the Library does.
  const renaming = useAnyRenaming()
  const renamePending = useFloatingRenamePending(windowId)
  const held = popupHeld || libraryOpen || renaming || renamePending
  const heldRef = useRef(held)
  heldRef.current = held
  const libraryOnlyRef = useRef(false)
  libraryOnlyRef.current = libraryOpen && !popupHeld
  const children = childrenOf(surfaces, instance.instanceId)
  const [leaving, setLeaving] = useState(false)
  const [drags, setDrags] = useState(0)
  const hover = useRef<PeekHover | null>(null)
  const seen = useRef(false)
  const slideOut = useRef<ReturnType<typeof setTimeout> | null>(null)
  const reducedRef = useRef(reduced)
  reducedRef.current = reduced
  const props = instance.props
  const pinned = typeof props === 'object' && props !== null && !Array.isArray(props) && props.pinned === true
  const pinnedRef = useRef(pinned)
  pinnedRef.current = pinned
  // The panel mounts at its slide-in start and moves on the second animation frame. The passive overlay's widget
  // shows after the state that mounts the peek, and a hidden page draws no frames, so the slide starts on the first
  // frame the widget draws and the one before it has the start position on screen.
  const [entered, setEntered] = useState(reduced || pinned)
  useEffect(() => {
    if (entered) return
    let frame = requestAnimationFrame(() => {
      frame = requestAnimationFrame(() => setEntered(true))
    })
    return () => cancelAnimationFrame(frame)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Slides out, then asks main to close it. Main's grace starts this through props.leaving, on its clock, so a
  // busy overlay cannot start the retract late; the overlay's own grace below is the same rule.
  const slideAway = useRef(() => {})
  slideAway.current = () => {
    if (pinnedRef.current || slideOut.current) return
    setLeaving(true)
    slideOut.current = setTimeout(() => close('slide-out done'), reducedRef.current ? 0 : PEEK_SLIDE_OUT_DONE_MS)
  }
  const leavingProp = typeof props === 'object' && props !== null && !Array.isArray(props) && props.leaving === true
  const leavingPropSeen = useRef(false)
  useEffect(() => {
    if (leavingProp) {
      trace("main's grace ran out: slides out")
      slideAway.current()
    }
    else if (leavingPropSeen.current && slideOut.current) {
      // Main saw the pointer come back onto the panel during the slide: it slides back in from where it is.
      clearTimeout(slideOut.current)
      slideOut.current = null
      setLeaving(false)
    }
    leavingPropSeen.current = leavingProp
  }, [leavingProp])

  useEffect(() => {
    const h = createPeekHover(() => {
      trace("the overlay's grace ran out: slides out")
      slideAway.current()
    })
    hover.current = h
    // Only after a real pointer event: a peek the pointer left before it showed must reach the 5 s guard.
    const beat = setInterval(() => {
      if (seen.current && h.hovered) void clu().shell.pointerAlive()
    }, PEEK_ALIVE_MS)
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape' || pinnedRef.current || e.defaultPrevented) return
      if (!heldRef.current) close('Escape')
      // Held by its Library, Escape closes the Library so a held peek always has a way out; the peek then retracts
      // once the pointer is off it. A pop-up row's menu or Rename keeps Escape for itself.
      else if (libraryOnlyRef.current && !keepsEscape(e.target)) {
        trace('Escape closes the Library')
        void clu().archive.close()
      }
    }
    window.addEventListener('keydown', onKey)
    // The peek has no tab drag (decision 25); a start here would be a bug, so it is counted where tests can see it.
    const offDrag = dragBus.on('start', () => setDrags((n) => n + 1))
    return () => {
      h.dispose()
      hover.current = null
      clearInterval(beat)
      if (slideOut.current) clearTimeout(slideOut.current)
      window.removeEventListener('keydown', onKey)
      offDrag()
      // A rename the peek closed under ends with it, so the next peek does not open held.
      setRenaming(null)
    }
  }, [])

  const childKey = children.map((c) => `${c.instanceId}:${c.layer}`).join(',')
  const lastChildren = useRef<OverlaySurfaceState[]>([])
  const holdSent = useRef(false)
  useEffect(() => {
    // A child in the top layer (a menu) covers the peek and takes the pointer: once it closes, the peek counts as
    // left until the pointer moves over it again.
    const gone = lastChildren.current.filter((p) => !children.some((c) => c.instanceId === p.instanceId))
    hover.current?.setChildren(children.length, gone.some((p) => p.layer === 'top'))
    lastChildren.current = children
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [childKey])

  useEffect(() => {
    if (held || holdSent.current) trace(`hold ${String(held)}, hovered ${String(hover.current?.hovered)}`)
    hover.current?.setHeld(held)
    // The browser holds the peek's widget the same way and hands it the keyboard; a new peek starts released.
    if (held || holdSent.current) void clu().shell.holdSidebarPeek({ held })
    holdSent.current = held
  }, [held])

  const inside = () => {
    const h = hover.current
    if (!h) return
    if (!seen.current || !h.hovered) void clu().shell.pointerAlive()
    seen.current = true
    if (slideOut.current) {
      clearTimeout(slideOut.current)
      slideOut.current = null
      setLeaving(false)
    }
    h.enter()
  }

  const style = {
    width,
    '--peek-in': `${reduced || pinned ? 0 : PEEK_SLIDE_IN_MS}ms`,
    '--peek-out': `${reduced ? 0 : PEEK_SLIDE_OUT_MS}ms`,
    '--peek-out-ease': PEEK_SLIDE_OUT_EASE
  } as CSSProperties

  return (
    <div
      className="clu-peek"
      data-shell="sidebar-peek"
      data-leaving={String(leaving)}
      data-motion={peekMotion(entered, leaving)}
      data-pinned={String(pinned)}
      data-drag-starts={drags}
      data-children={children.length}
      data-held={String(held)}
      style={style}
      onPointerEnter={inside}
      onPointerMove={inside}
      onPointerLeave={() => {
        trace(`pointer leave, held ${String(heldRef.current)}`)
        hover.current?.leave()
      }}
      onContextMenuCapture={(e) => {
        // The row menu opens from the peek, so the peek stays open under it (PLAN 3.3 openedFrom).
        const row = e.target instanceof Element ? e.target.closest('[data-tab-id]') : null
        const tabId = row instanceof HTMLElement ? row.dataset.tabId : undefined
        if (!row || !tabId) return
        e.preventDefault()
        e.stopPropagation()
        const r = row.getBoundingClientRect()
        const anchor = { x: Math.round(r.left), y: Math.round(r.top), width: Math.round(r.width), height: Math.round(r.height) }
        void clu().tabs.openMenu({ menu: 'row', nodeId: tabId, anchor, openedFrom: instance.instanceId, light: menuLight() })
      }}
    >
      <ThemeBackground />
      <Sidebar mode="peek" />
      <WindowLights focused={focused} />
    </div>
  )
}
