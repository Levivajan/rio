// Space slider (docs/plan/03-spaces.md "Sliding animation"): on a switch the outgoing and incoming
// panels slide side by side for 240 ms on a critically damped spring (M11; a higher-index
// target slides content left), or cross-fade for 120 ms under
// reduced motion. The page view does not animate; main swaps it when the switch commits. During a swipe the current
// panel and its neighbor follow the fingers; a commit settles from there once main has switched (at the
// start of the settle) on a curve that carries on at the release speed, a cancel springs back on one critically damped
// spring from the release velocity (docs/chromium/motion.md "Space swipe"). data-state is idle | dragging | settling for tests.
import { cloneElement, isValidElement, useLayoutEffect, useMemo, useRef, useState, type CSSProperties, type ReactElement, type ReactNode } from 'react'
import { cssLinear, LOOK } from '../../../shared/look/tokens'
import { sameItems, useCluState } from '../../common/useCluState'
import { exitCurve, settleStart, springCurve } from './swipeReducer'
import { useSpaceSwipe } from './useSpaceSwipe'
import './spaces.css'

export type SliderState = 'idle' | 'dragging' | 'settling'
/** `from` is how far the swipe had already moved the panels (0 for clicks and keys). */
interface Outgoing { spaceId: string; node: ReactNode; dir: 1 | -1; from: number; spring?: { ms: number; easing: string }; elapsed?: number }

/** A swipe's spring back: one spring over `distance` px from the release velocity toward the target. */
function swipeSpring(distance: number, toward: number): { ms: number; easing: string } {
  const { ms, curve } = springCurve(distance, toward)
  return { ms, easing: cssLinear(curve) }
}

/** A commit's settle over `distance` px: it carries on at the release speed and eases in (the
 *  spring's second frame stepped 11.7 to 19 px after a slow drag's 3 px frames). */
function swipeSettle(distance: number, toward: number): { ms: number; easing: string } {
  const { ms, curve } = exitCurve(distance, toward)
  return { ms, easing: cssLinear(curve) }
}

/** The slide: a spring with no overshoot, as a linear() curve over the slide's duration. */
const SLIDE_EASING = cssLinear(LOOK.motion.spaceSlideCurve)
/** A commit whose switch never arrives springs back after this long. */
const COMMIT_WAIT_MS = 500

/** A duration token in ms (`--t-slow`, `--t-fade`), so reduced motion applies here too. */
function tokenMs(name: string, fallback: number): number {
  // Chromium may serialize the token as seconds ('0.26s') or milliseconds ('260ms').
  const m = /^\s*(\d*\.?\d+)(ms|s)\s*$/.exec(getComputedStyle(document.documentElement).getPropertyValue(name))
  return m ? Math.round(Number(m[1]) * (m[2] === 's' ? 1000 : 1)) : fallback
}

const shift = (n: number) => `translateX(${n * 100}%)`

export function SpaceSlider({ spaceId, children }: { spaceId: string; children: ReactNode }) {
  const order = useCluState((s) => s.spaceOrder, sameItems)
  const [shown, setShown] = useState(spaceId)
  const [outgoing, setOutgoing] = useState<Outgoing | null>(null)
  const root = useRef<HTMLDivElement>(null)
  const { view, settled, springBack } = useSpaceSwipe(root, spaceId, order)
  // The last committed children, i.e. the panel for `shown`, kept so it can render as the outgoing panel.
  const lastNode = useRef<ReactNode>(children)
  const incoming = useRef<HTMLDivElement>(null)
  const leaving = useRef<HTMLDivElement>(null)
  const peerRef = useRef<HTMLDivElement>(null)

  if (shown !== spaceId) {
    const from = order.indexOf(shown)
    const to = order.indexOf(spaceId)
    setShown(spaceId)
    if (view?.mode === 'committing' && view.peer === spaceId && view.width > 0) {
      // Main switched at the start of the settle: the swiped panels carry on from where the fingers left them.
      settled()
      const dir = view.travel < 0 ? -1 : 1
      const share = Math.min(1, Math.abs(view.travel) / view.width)
      const spring = swipeSettle((1 - share) * view.width, (view.velocity ?? 0) * dir)
      // The settle began at the lift; it carries on at the same point of the same curve, with no held frame.
      const elapsed = settleStart(view.at, performance.now(), spring.ms)
      setOutgoing({ spaceId: shown, node: lastNode.current, dir, from: share, spring, elapsed })
    } else {
      if (view) settled()
      setOutgoing(shown && spaceId ? { spaceId: shown, node: lastNode.current, dir: from >= 0 && to >= 0 && to < from ? -1 : 1, from: 0 } : null)
    }
  } else if (view?.mode === 'dragging' && outgoing) {
    // A drag that starts while a click's slide is still running takes over from it.
    setOutgoing(null)
  }

  useLayoutEffect(() => {
    lastNode.current = children
  })

  // Layout effect: the incoming panel never paints at rest before its animation starts.
  useLayoutEffect(() => {
    if (!outgoing) return
    const inc = incoming.current
    const out = leaving.current
    const finish = () => setOutgoing((cur) => (cur === outgoing ? null : cur))
    if (!inc || !out || typeof inc.animate !== 'function') return finish()
    // A swipe's settle always slides: the panels already follow the fingers, so a cross-fade would jump.
    const swiped = outgoing.from > 0
    const reduced = !swiped && document.documentElement.dataset.reducedMotion === 'true'
    const spring = swiped ? outgoing.spring : undefined
    const duration = spring ? spring.ms : reduced ? tokenMs('--t-fade', 120) : tokenMs('--t-slow', LOOK.motion.spaceSlideMs)
    const timing: KeyframeAnimationOptions = { duration, easing: spring ? spring.easing : reduced ? 'linear' : SLIDE_EASING, fill: 'both' }
    const { dir, from } = outgoing
    const animations = reduced
      ? [inc.animate([{ opacity: 0 }, { opacity: 1 }], timing), out.animate([{ opacity: 1 }, { opacity: 0 }], timing)]
      : [inc.animate([{ transform: shift(dir * (1 - from)) }, { transform: shift(0) }], timing), out.animate([{ transform: shift(-dir * from) }, { transform: shift(-dir) }], timing)]
    if (spring && outgoing.elapsed) for (const a of animations) a.currentTime = Math.min(duration, outgoing.elapsed)
    animations[0]?.finished.then(finish, () => undefined)
    // A window the compositor does not draw may not tick animations; never stay settling.
    const fallback = setTimeout(finish, duration + 120)
    return () => {
      clearTimeout(fallback)
      for (const a of animations) a.cancel()
    }
  }, [outgoing])

  // A cancelled swipe springs back from where the fingers left the panels.
  const springing = view?.mode === 'springing' ? view : null
  useLayoutEffect(() => {
    if (!springing) return
    const cur = incoming.current
    if (!cur || typeof cur.animate !== 'function') return settled()
    const s = springing.width > 0 ? springing.travel / springing.width : 0
    const dir = s < 0 ? -1 : 1
    // Back to rest: the speed toward it is the release velocity against the travel's direction.
    const spring = swipeSpring(Math.abs(springing.travel), -(springing.velocity ?? 0) * dir)
    const timing: KeyframeAnimationOptions = { duration: spring.ms, easing: spring.easing, fill: 'both' }
    const animations = [cur.animate([{ transform: shift(-s) }, { transform: shift(0) }], timing)]
    if (peerRef.current) animations.push(peerRef.current.animate([{ transform: shift(dir - s) }, { transform: shift(dir) }], timing))
    animations[0]?.finished.then(settled, () => undefined)
    const fallback = setTimeout(settled, spring.ms + 120)
    return () => {
      clearTimeout(fallback)
      for (const a of animations) a.cancel()
    }
  }, [springing, settled])

  // A commit settles from the frame after the lift (it held 50 ms for main's switch): the panels run the
  // settle's spring at once, and the switch picks the same spring up where it got to. If the switch never arrives,
  // they spring back. A commit into the Library holds; the Library replaces the list.
  const committing = view?.mode === 'committing' ? view : null
  useLayoutEffect(() => {
    if (!committing) return
    const t = setTimeout(springBack, COMMIT_WAIT_MS)
    const cur = incoming.current
    const next = peerRef.current
    if (!committing.peer || !cur || !next || typeof cur.animate !== 'function' || committing.width <= 0) return () => clearTimeout(t)
    const dir = committing.travel < 0 ? -1 : 1
    const share = Math.min(1, Math.abs(committing.travel) / committing.width)
    const spring = swipeSettle((1 - share) * committing.width, (committing.velocity ?? 0) * dir)
    const timing: KeyframeAnimationOptions = { duration: spring.ms, easing: spring.easing, fill: 'both' }
    const animations = [cur.animate([{ transform: shift(-dir * share) }, { transform: shift(-dir) }], timing), next.animate([{ transform: shift(dir * (1 - share)) }, { transform: shift(0) }], timing)]
    // One frame in: the first frame after the lift moves from where the last drag frame left the strip.
    const start = settleStart(committing.at, performance.now(), spring.ms)
    for (const a of animations) a.currentTime = start
    return () => {
      clearTimeout(t)
      for (const a of animations) a.cancel()
    }
  }, [committing, springBack])

  // The neighbor beside the current panel during a swipe: the same tab list, for that space.
  const peer = view?.peer ?? null
  const peerNode = useMemo(() => (peer && isValidElement(children) ? cloneElement(children as ReactElement<{ spaceId: string }>, { spaceId: peer }) : null), [children, peer])
  const s = view && view.width > 0 ? Math.max(-1, Math.min(1, view.travel / view.width)) : 0
  const dir = s < 0 ? -1 : 1
  const follow = (n: number): CSSProperties | undefined => (view ? { transform: shift(n) } : undefined)

  // One keyed array, so the outgoing panel keeps its DOM (and scroll position) from when it was the shown one. The
  // peer comes after the current panel, so a commit keeps both in the same order and keeps their DOM.
  const panels = [
    outgoing ? (
      <div key={outgoing.spaceId} ref={leaving} className="clu-space-panel" data-space-panel={outgoing.spaceId} data-outgoing="true" aria-hidden="true" inert>
        {outgoing.node}
      </div>
    ) : null,
    <div key={spaceId} ref={incoming} className="clu-space-panel" data-space-panel={spaceId} style={follow(-s)}>
      {children}
    </div>,
    view && peer && peerNode && peer !== spaceId ? (
      <div key={peer} ref={peerRef} className="clu-space-panel" data-space-panel={peer} data-peer="true" aria-hidden="true" inert style={follow(dir - s)}>
        {peerNode}
      </div>
    ) : null
  ]
  const state: SliderState = view?.mode === 'dragging' ? 'dragging' : outgoing || view ? 'settling' : 'idle'
  return (
    <div ref={root} className="clu-space-slider" data-state={state}>
      {panels}
    </div>
  )
}
