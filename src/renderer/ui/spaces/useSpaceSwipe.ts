// Swipe binding (spaces-t4): wheel events on the sidebar root only (passive: false) run swipeReducer;
// the drag goes to main as spaces.swipe so windows[id].swipe follows the fingers, and 'end' switches the space.
// Swipes over the page never reach this view, so pages keep their own gestures. The slider draws `view`. Past the
// first space the Library waits: the panel follows the fingers right and a commit opens the Library.
// The drag never re-renders per wheel event ("jumpy"): `begin` and a change of neighbor set React state,
// every other update only records the travel, and one requestAnimationFrame per display frame writes the panels'
// transforms and sends spaces.swipe. The gesture state is the sidebar's shared one (`sharedSwipe`).
import { useCallback, useEffect, useLayoutEffect, useRef, useState, type RefObject } from 'react'
import { leaveSwipe, opensLibrary, sharedSwipe, swipeEdges, swipeReducer, type SwipeEffect, type SwipeInput } from './swipeReducer'
import { clu } from '../../common/clu'
import { useCluState, useWindowId } from '../../common/useCluState'
import { blendOf, setSwipeBlend } from '../theme/swipeBlend'

export interface SwipeView {
  mode: 'dragging' | 'committing' | 'springing'
  /** Panel travel in px, positive toward the next space. */
  travel: number
  width: number
  /** The neighbor drawn beside the current panel (null past the first or last space). */
  peer: string | null
  /** The release's finger velocity in px/ms (positive toward the next space), for the settle's spring. */
  velocity?: number
  /** When a commit started settling (performance.now()), so the settle keeps its timeline when main's switch lands. */
  at?: number
}

const LINE_PX = 16

/** The space beside `spaceId` in the direction of `dir`'s sign. */
function neighborOf({ spaceId, order }: { spaceId: string; order: readonly string[] }, dir: number): string | null {
  const at = order.indexOf(spaceId)
  return at < 0 || dir === 0 ? null : (order[at + Math.sign(dir)] ?? null)
}

/** The event's own time on the performance clock; junk or a missing stamp falls back to now. */
export function eventTime(e: { timeStamp: number }): number {
  const now = performance.now()
  return Number.isFinite(e.timeStamp) && e.timeStamp > 0 && e.timeStamp <= now ? e.timeStamp : now
}

/**
 * The window's trackpad phases from the browser (windows[id].wheel): the returned ref says whether the device
 * reports real phases, and `onLift` runs when the fingers leave the trackpad. One render per gesture, never per event.
 */
export function useWheelLift(onLift: () => void): RefObject<boolean> {
  const windowId = useWindowId() ?? ''
  const phased = useCluState((s) => s.windows[windowId]?.wheel?.phased === true)
  const lifts = useCluState((s) => s.windows[windowId]?.wheel?.lifts ?? 0)
  const phasedRef = useRef(phased)
  const liftRef = useRef(onLift)
  useLayoutEffect(() => {
    phasedRef.current = phased
    liftRef.current = onLift
  })
  const seen = useRef(lifts)
  // A layout effect: the release and the settle's first frame land before the next paint, not a frame later.
  useLayoutEffect(() => {
    if (lifts === seen.current) return
    seen.current = lifts
    liftRef.current()
  }, [lifts])
  return phasedRef
}

function send(phase: 'begin' | 'update' | 'end' | 'cancel', dx: number): void {
  void clu().spaces.swipe({ phase, dx, dy: 0, momentum: false }).catch(() => undefined)
}

export function useSpaceSwipe(slider: RefObject<HTMLElement | null>, spaceId: string, order: readonly string[]) {
  const [view, setView] = useState<SwipeView | null>(null)
  const viewRef = useRef<SwipeView | null>(null)
  const frame = useRef(0)
  const where = useRef({ spaceId, order })
  useLayoutEffect(() => {
    where.current = { spaceId, order }
  })

  const show = useCallback((v: SwipeView | null) => {
    viewRef.current = v
    setView(v)
    setSwipeBlend(blendOf(v))
  }, [])

  // The panels' transforms in px, written in the wheel handler itself, so the next frame moves them by exactly the
  // wheel's delta on the compositor. Nothing goes to the browser per frame (the per-frame spaces.swipe state
  // round trip re-rendered the sidebar and stalled the drag for up to 183 ms).
  const paint = useCallback(() => {
    const v = viewRef.current
    const root = slider.current
    if (!v || v.mode !== 'dragging' || !root) return
    const t = Math.max(-v.width, Math.min(v.width, v.travel))
    const cur = root.querySelector<HTMLElement>('.clu-space-panel:not([data-outgoing]):not([data-peer])')
    if (cur) cur.style.transform = `translateX(${-t}px)`
    const peer = root.querySelector<HTMLElement>('.clu-space-panel[data-peer]')
    if (peer) peer.style.transform = `translateX(${(t < 0 ? -v.width : v.width) - t}px)`
    // The theme blend follows the fingers in the same call: ThemeBackground writes its style, no render.
    setSwipeBlend(blendOf(v))
  }, [slider])

  const apply = useCallback((effect: SwipeEffect | null, width: number) => {
    if (!effect) return
    if (effect.kind === 'begin' || effect.kind === 'update') {
      const peer = neighborOf(where.current, effect.travel)
      const v = viewRef.current
      if (effect.kind === 'update' && v?.mode === 'dragging' && v.peer === peer && v.width === width) {
        // The same view object stays in state, so a render for any other reason draws the latest travel too.
        v.travel = effect.travel
        paint()
        return
      }
      show({ mode: 'dragging', travel: effect.travel, width, peer })
      return
    }
    if (effect.kind === 'commit' && opensLibrary(where.current.order.indexOf(where.current.spaceId), effect.step)) {
      // The Library replaces the space list; if it never opens, the commit wait springs the panel back.
      show({ mode: 'committing', travel: effect.travel, width, peer: null })
      send('cancel', 0)
      void clu().archive.open().catch(() => undefined)
    } else if (effect.kind === 'commit') {
      show({ mode: 'committing', travel: effect.travel, width, peer: neighborOf(where.current, effect.step), velocity: sharedSwipe.state.velocity, at: performance.now() })
      send('end', effect.travel || effect.step)
    } else {
      const v = viewRef.current
      show(v && effect.travel !== 0 ? { ...v, mode: 'springing', travel: effect.travel, velocity: sharedSwipe.state.velocity } : null)
      send('cancel', effect.travel)
    }
  }, [paint, show])

  // `now` is the wheel event's own timestamp, so events queued behind a busy frame keep their real 8 ms spacing and
  // never read as the fingers pausing. The idle end is checked once per display frame: Chromium dispatches
  // queued wheel events before the frame's callbacks, so a slow frame never ends a gesture whose fingers still move.
  const dispatch = useCallback(
    (input: SwipeInput, width: number, now: number) => {
      const next = swipeReducer(sharedSwipe.state, input, now)
      sharedSwipe.state = next
      apply(next.effect, width)
      const { phase } = next
      if ((phase === 'pending' || phase === 'dragging' || phase === 'ignored') && !frame.current) {
        const watch = () => {
          frame.current = 0
          const s = dispatch({ kind: 'tick' }, width, performance.now())
          if (s.phase !== 'pending' && s.phase !== 'dragging' && s.phase !== 'ignored') return
          if (!frame.current) frame.current = requestAnimationFrame(watch)
        }
        frame.current = requestAnimationFrame(watch)
      }
      return next
    },
    [apply]
  )

  const phased = useWheelLift(() => dispatch({ kind: 'lift' }, viewRef.current?.width ?? 0, performance.now()))

  useEffect(() => {
    const root = slider.current?.closest<HTMLElement>('.clu-sidebar')
    // The hover-peek copy of the sidebar (overlay bundle) and private windows have no swipe.
    if (!root || root.dataset.mode !== 'main') return
    const onWheel = (e: WheelEvent) => {
      // The panels' own width, the one their % transforms use: the sidebar's is 2 insets wider, and the commit's first
      // render stepped the strip 5.5 px and closed the panel gap from 214 to 198 px.
      const width = slider.current?.getBoundingClientRect().width || root.getBoundingClientRect().width
      const unit = e.deltaMode === 1 ? LINE_PX : e.deltaMode === 2 ? width : 1
      const { spaceId: cur, order: ids } = where.current
      const s = dispatch(
        { kind: 'wheel', dx: e.deltaX * unit, dy: e.deltaY * unit, ctrlKey: e.ctrlKey, shiftKey: e.shiftKey, width, phased: phased.current, ...swipeEdges(ids.indexOf(cur), ids.length) },
        width,
        eventTime(e)
      )
      if (s.handled) e.preventDefault()
    }
    root.addEventListener('wheel', onWheel, { passive: false })
    return () => {
      root.removeEventListener('wheel', onWheel)
      cancelAnimationFrame(frame.current)
      frame.current = 0
      if (sharedSwipe.state.phase === 'dragging') send('cancel', 0)
      leaveSwipe()
      setSwipeBlend(null)
    }
  }, [slider, dispatch, phased])

  // The active space changed under a running drag (a key or a click): drop the rest of that gesture.
  useEffect(() => {
    if (sharedSwipe.state.phase !== 'dragging') return
    sharedSwipe.state = swipeReducer(sharedSwipe.state, { kind: 'abandon' }, performance.now())
    cancelAnimationFrame(frame.current)
    frame.current = 0
    send('cancel', 0)
    show(null)
  }, [spaceId, show])

  /** The slider finished drawing the settle or spring back. */
  const settled = useCallback(() => show(null), [show])
  /** A commit that never switched (the target went away): spring back instead. */
  const springBack = useCallback(() => {
    const v = viewRef.current
    // A settle that ran to the neighbor springs back from there; a Library commit held where the fingers left it.
    if (v && v.mode === 'committing') show({ ...v, mode: 'springing', ...(v.peer ? { travel: v.travel < 0 ? -v.width : v.width, velocity: 0 } : {}) })
  }, [show])
  return { view, settled, springBack }
}
