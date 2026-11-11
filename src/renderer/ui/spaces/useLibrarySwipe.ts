// Swipe out of the Library: the Library sits before the first space, so fingers moving left over the open
// Library (wheel deltaX > 0) slide it out the way they came. The same swipeReducer runs with only a next side: the
// Library follows the fingers, a commit closes it (main narrows the sidebar back), a cancel springs it back on the
// space slide's curve. data-swipe on the element is dragging | settling while it moves, for tests. The transform is
// written once per display frame, and the gesture state is the sidebar's shared one, so the momentum of the swipe
// that closes the Library never moves the space list that mounts in its place.
import { useLayoutEffect, useEffect, useRef, useState, type RefObject } from 'react'
import { cssLinear, LOOK } from '../../../shared/look/tokens'
import { easeOutExp, LIBRARY_TWEEN } from '../../../shared/shell/tween'
import { leaveSwipe, libraryExit, liveExit, settleStart, sharedSwipe, SWIPE, swipeReducer, type LibraryExit, type SwipeInput } from './swipeReducer'
import { clu } from '../../common/clu'
import { eventTime, useWheelLift } from './useSpaceSwipe'

const LINE_PX = 16
const SLIDE_EASING = cssLinear(LOOK.motion.spaceSlideCurve)

export function useLibrarySwipe(ref: RefObject<HTMLElement | null>, active: boolean) {
  const lift = useRef<(() => void) | null>(null)
  const phased = useWheelLift(() => lift.current?.())
  useEffect(() => {
    const el = ref.current
    if (!active || !el) return
    let anim: Animation | undefined
    let frame = 0
    // Written in the wheel handler, so the next frame moves the Library by exactly the wheel's delta.
    const place = (travel: number) => {
      el.style.transform = travel > 0 ? `translateX(${-travel}px)` : ''
    }
    // `now` is the event's own time and the idle end is checked once per frame, as in useSpaceSwipe: a slow frame
    // never reads as the fingers pausing (the Library sprang back after 16 px).
    const dispatch = (input: SwipeInput, now: number) => {
      const state = swipeReducer(sharedSwipe.state, input, now)
      sharedSwipe.state = state
      const effect = state.effect
      if (effect?.kind === 'begin' || effect?.kind === 'update') {
        anim?.cancel()
        el.dataset.swipe = 'dragging'
        place(effect.travel)
      } else if (effect?.kind === 'commit') {
        // The Library carries on out from where the fingers left it, on the exit curve the sidebar that replaces it
        // rides too (it held, then swapped for the space list in one frame).
        const exit = libraryExit(effect.travel, el.getBoundingClientRect().width, state.velocity, performance.now())
        place(exit.from)
        el.dataset.swipe = 'settling'
        if (typeof el.animate === 'function' && exit.width > exit.from) {
          anim = el.animate([{ transform: `translateX(${-exit.from}px)` }, { transform: `translateX(${-exit.width}px)` }], { duration: exit.ms, easing: cssLinear(exit.curve), fill: 'forwards' })
          // One frame in, so the frame after the lift already moves (it used to hold at -260).
          anim.currentTime = settleStart(exit.at, performance.now(), exit.ms)
        }
        sharedSwipe.exit = exit
        void clu().archive.close().catch(() => undefined)
      } else if (effect?.kind === 'cancel') {
        place(0)
        if (effect.travel > 0 && typeof el.animate === 'function') {
          el.dataset.swipe = 'settling'
          anim = el.animate([{ transform: `translateX(${-effect.travel}px)` }, { transform: 'translateX(0)' }], { duration: SWIPE.settleMs, easing: SLIDE_EASING })
          anim.finished.then(() => delete el.dataset.swipe, () => undefined)
        } else delete el.dataset.swipe
      }
      const live = (p: string) => p === 'pending' || p === 'dragging' || p === 'ignored'
      if (live(state.phase) && !frame) {
        const watch = () => {
          frame = 0
          if (live(dispatch({ kind: 'tick' }, performance.now()).phase) && !frame) frame = requestAnimationFrame(watch)
        }
        frame = requestAnimationFrame(watch)
      }
      return state
    }
    const onWheel = (e: WheelEvent) => {
      const width = el.getBoundingClientRect().width
      const unit = e.deltaMode === 1 ? LINE_PX : e.deltaMode === 2 ? width : 1
      const s = dispatch({ kind: 'wheel', dx: e.deltaX * unit, dy: e.deltaY * unit, ctrlKey: e.ctrlKey, shiftKey: e.shiftKey, width, hasPrev: false, hasNext: true, phased: phased.current }, eventTime(e))
      if (s.handled) e.preventDefault()
    }
    el.addEventListener('wheel', onWheel, { passive: false })
    lift.current = () => dispatch({ kind: 'lift' }, performance.now())
    return () => {
      lift.current = null
      el.removeEventListener('wheel', onWheel)
      cancelAnimationFrame(frame)
      anim?.cancel()
      place(0)
      delete el.dataset.swipe
      leaveSwipe()
    }
  }, [ref, active, phased])
}

/**
 * The rest of a swipe out of the Library once it has closed: the sidebar `nav` slides in on the exit's curve
 * from the Library's right edge, and the returned exit asks the sidebar to draw the Library just left of itself, so
 * both keep moving as one strip from the frame the Library closed. Null when no swipe closed it or the slide is done.
 */
/** The browser's Library tween (CluShell::Relayout, kLibraryMs, TweenCurve::kExp) as a linear() curve, sampled every
 *  LIBRARY_EASING_STEP_MS so the compositor's 120 Hz frames between samples stay on the browser's continuous curve. */
const LIBRARY_EASING_STEP_MS = 4
export const LIBRARY_NARROW_EASING = cssLinear(
  Array.from({ length: Math.ceil(LIBRARY_TWEEN.ms / LIBRARY_EASING_STEP_MS) + 1 }, (_, i) => {
    const t = Math.min(LIBRARY_TWEEN.ms, i * LIBRARY_EASING_STEP_MS)
    return [t, Math.round(easeOutExp(t / LIBRARY_TWEEN.ms) * 1000) / 1000] as [number, number]
  })
)

export function useLibraryExit(nav: RefObject<HTMLElement | null>, library: boolean, width?: number): LibraryExit | null {
  const [exit, setExit] = useState<LibraryExit | null>(null)
  const [was, setWas] = useState(library)
  // The width the Library had, so the sidebar that replaces it narrows from there.
  const [narrow, setNarrow] = useState<{ from: number; to: number } | null>(null)
  // The last width drawn and whether it was the Library's: the closed state and the narrower width can land in the
  // same patch or one after the other, and either way the first narrower width after the Library starts the narrow.
  const [shown, setShown] = useState({ width: width ?? 0, library, libraryWidth: library, at: 0 })
  if (was !== library) {
    // Set during the render that drops the Library, so no frame shows the sidebar at rest first.
    setWas(library)
    setExit(library ? null : liveExit(sharedSwipe.exit, performance.now()))
  }
  const w = width !== undefined && Number.isFinite(width) && width > 0 ? width : 0
  if (w && (shown.width !== w || shown.library !== library)) {
    if (library) setNarrow(null)
    else if (shown.libraryWidth && shown.width > w && (shown.at === 0 || performance.now() - shown.at < LIBRARY_TWEEN.ms)) setNarrow({ from: shown.width, to: w })
    // Still the Library's width (the state's width comes in a later patch): keep it marked as the Library's for one
    // tween's time, so a later sidebar resize never reads as the Library closing.
    const pending = !library && shown.library && shown.width === w
    setShown({ width: w, library, libraryWidth: library || pending, at: pending ? performance.now() : 0 })
  }
  // The browser narrows the page card from the Library's width over kLibraryMs on its exponential curve, and the
  // sidebar's state width is the target at once: the nav's width rides the same curve, so its edge stays on the card's
  // edge with no one-frame step.
  useLayoutEffect(() => {
    const el = nav.current
    if (!narrow || !el || typeof el.animate !== 'function') return
    if (typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches) return
    const anim = el.animate([{ width: `${narrow.from}px` }, { width: `${narrow.to}px` }], { duration: LIBRARY_TWEEN.ms, easing: LIBRARY_NARROW_EASING })
    anim.finished.then(() => setNarrow((cur) => (cur === narrow ? null : cur)), () => undefined)
    return () => anim.cancel()
  }, [nav, narrow])
  useLayoutEffect(() => {
    if (!library) sharedSwipe.exit = null
  }, [library])
  useLayoutEffect(() => {
    const el = nav.current
    if (!exit || !el) return
    const done = () => setExit((cur) => (cur === exit ? null : cur))
    if (typeof el.animate !== 'function') return done()
    const anim = el.animate([{ transform: `translateX(${exit.width - exit.from}px)` }, { transform: 'translateX(0)' }], { duration: exit.ms, easing: cssLinear(exit.curve) })
    // Pick up the Library's slide where it is going to be this frame, never one frame behind.
    anim.currentTime = settleStart(exit.at, performance.now(), exit.ms)
    anim.finished.then(done, () => undefined)
    const fallback = setTimeout(done, exit.ms + 120)
    return () => {
      clearTimeout(fallback)
      anim.cancel()
    }
  }, [nav, exit])
  return exit
}
