// Peek motion (05 Peek timings): the card unfolds from a wide short band at the clicked link,
// and the close image shrinks back toward the click point. Pure keyframe
// math, plus the token reads the chrome uses for durations and easing.
import { LOOK } from '../../../shared/look/tokens'
import type { Rect } from '../../../shared/types'

/** Card open and close durations (05 Timings); the scrim uses --t-base. Both drop to 0 under reduced motion. */
export const OPEN_MS = LOOK.peek.openMs
export const CLOSE_MS = 120
/** The card starts at least this large so the first frames read as a card, not a dot. */
const MIN_SCALE = 0.1
const FALLBACK_EASE = 'cubic-bezier(0.2, 0, 0, 1)'

export interface CardMotion { transformOrigin: string; keyframes: Keyframe[] }

/** transform-origin for the card: the origin rect's center, relative to the card's top-left corner. */
export function originWithin(origin: Rect, card: Rect): string {
  const x = Math.round(origin.x + origin.width / 2 - card.x)
  const y = Math.round(origin.y + origin.height / 2 - card.y)
  return `${x}px ${y}px`
}

function startScale(origin: Rect, card: Rect): string {
  const sx = Math.min(1, Math.max(MIN_SCALE, card.width > 0 ? origin.width / card.width : 1))
  const sy = Math.min(1, Math.max(MIN_SCALE, card.height > 0 ? origin.height / card.height : 1))
  return `scale(${+sx.toFixed(4)}, ${+sy.toFixed(4)})`
}

/** The first open frame: 943.5 x 242.5 for an 853 x 776 card, at full opacity. */
export const OPEN_BAND = { width: 943.5 / 853, height: 242.5 / 776 }
/** The open curve: 31% of the height at 0, 63% at 33 ms, 95.5% at 108 ms of 185. */
export const OPEN_EASE = 'cubic-bezier(0.25, 0.7, 0.4, 1)'
/** The rail's buttons show this long after the card starts, in one or two frames. */
export const RAIL_DELAY_MS = 108
export const RAIL_FADE_MS = 17

const round = (n: number) => +n.toFixed(4)

/** Unfolds the card from a wide short band centered on it, at the origin's height, to its final rect. */
export function openMotion(origin: Rect, card: Rect): CardMotion {
  const bandW = card.width * OPEN_BAND.width
  const bandH = card.height * OPEN_BAND.height
  const mid = origin.y + origin.height / 2
  const top = Number.isFinite(mid) ? Math.min(Math.max(mid - bandH / 2, card.y), card.y + card.height - bandH) : card.y
  const dx = round((card.width - bandW) / 2)
  const dy = round(top - card.y)
  return {
    transformOrigin: '0px 0px',
    keyframes: [
      { transform: `translate(${dx}px, ${dy}px) scale(${round(OPEN_BAND.width)}, ${round(OPEN_BAND.height)})`, opacity: 1 },
      { transform: 'none', opacity: 1 }
    ]
  }
}

/** Shrinks the captured page from the card rect toward the origin. */
export function closeMotion(origin: Rect, card: Rect): CardMotion {
  return {
    transformOrigin: originWithin(origin, card),
    keyframes: [
      { transform: 'none', opacity: 1 },
      { transform: startScale(origin, card), opacity: 0 }
    ]
  }
}

/** A duration token in ms (`150ms` or `0.15s`), or `fallback` when the variable is missing. */
export function readMs(value: string, fallback: number): number {
  const v = value.trim()
  const n = Number.parseFloat(v)
  if (!Number.isFinite(n) || n < 0) return fallback
  return v.endsWith('ms') ? n : v.endsWith('s') ? n * 1000 : fallback
}

export function tokens(): { base: number; ease: string } {
  const css = getComputedStyle(document.documentElement)
  return { base: readMs(css.getPropertyValue('--t-base'), 150), ease: css.getPropertyValue('--ease-out').trim() || FALLBACK_EASE }
}

/** Runs a Web Animation and resolves when it finishes; resolves at once where animations are unavailable. */
export function play(el: Element | null, keyframes: Keyframe[], options: KeyframeAnimationOptions): { done: Promise<boolean>; cancel: () => void } {
  if (!el || typeof el.animate !== 'function') return { done: Promise.resolve(true), cancel: () => undefined }
  const anim = el.animate(keyframes, options)
  return {
    done: anim.finished.then(
      () => true,
      () => false
    ),
    cancel: () => anim.cancel()
  }
}
