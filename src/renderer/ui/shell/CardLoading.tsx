// Page loading (M11): a pill in the margin above the page card, centered on it. It pulses while the
// page waits for its first bytes, then widens into a 128 point track whose segment drains and refills until the load
// finishes; then the track fills, shrinks to its center and fades (docs/chromium/motion.md). While
// the page is not loading the same spot shows a grabber: a 48 x 4 pill in the window's
// drag area, so the card's top edge reads as a handle. It draws only while the pointer is in the margin
// above the card: at rest the margin is empty.
import { useEffect, useState, type CSSProperties } from 'react'
import { LOOK } from '../../../shared/look/tokens'
import { useCluState, useWindowId } from '../../common/useCluState'
import { useReducedMotion } from '../../common/useReducedMotion'

/** The waiting pulse runs until the engine reports progress past its first step. */
export const RECEIVING_AT = 0.15

export type LoadingPhase = 'waiting' | 'receiving' | 'done'
/** What the indicator shows, since when, and the opacity it had when this phase began (so a phase change continues
 *  from the pulse's current opacity instead of jumping). `pill`: the done fade starts from the waiting pill. */
export interface LoadingTrack { phase: LoadingPhase | null; since: number; from: number; pill: boolean }

export const IDLE: LoadingTrack = { phase: null, since: 0, from: 0, pill: false }

/** CSS ease-in-out, cubic-bezier(0.42, 0, 0.58, 1), solved for x by bisection. */
function easeInOut(x: number): number {
  const bez = (t: number, a: number, b: number) => 3 * a * t * (1 - t) ** 2 + 3 * b * t ** 2 * (1 - t) + t ** 3
  let lo = 0
  let hi = 1
  for (let i = 0; i < 30; i++) {
    const mid = (lo + hi) / 2
    if (bez(mid, 0.42, 0.58) < x) lo = mid
    else hi = mid
  }
  return bez((lo + hi) / 2, 0, 1)
}

/** The waiting pulse's opacity `ms` after it started: 0 to 1 to 0, ease-in-out each way (clu-loading-pulse). */
export function pulseOpacity(ms: number): number {
  if (!Number.isFinite(ms) || ms <= 0) return 0
  const p = (ms % LOOK.loading.pulseMs) / LOOK.loading.pulseMs
  return p < 0.5 ? easeInOut(p * 2) : 1 - easeInOut((p - 0.5) * 2)
}

function opacityAt(track: LoadingTrack, now: number): number {
  if (track.phase === 'waiting') return pulseOpacity(now - track.since)
  if (track.phase === 'receiving') return track.from + (1 - track.from) * Math.min(1, Math.max(0, (now - track.since) / LOOK.loading.openMs))
  return 0
}

/** The next track for the page's live phase (null: not loading). Pure: the same live phase keeps the same track, so
 *  a re-render or a repeated state patch never restarts an animation. */
export function nextLoadingTrack(track: LoadingTrack, live: 'waiting' | 'receiving' | null, now: number, reduced: boolean): LoadingTrack {
  if (live === null) {
    if (track.phase === null || track.phase === 'done') return track
    if (reduced) return IDLE
    return { phase: 'done', since: now, from: opacityAt(track, now), pill: track.phase === 'waiting' }
  }
  if (track.phase === live) return track
  // A new load after the done fade starts its pulse over; waiting to receiving widens from the pulse's opacity.
  return { phase: live, since: now, from: live === 'receiving' && track.phase === 'waiting' ? opacityAt(track, now) : 0, pill: false }
}

export function CardLoading() {
  const windowId = useWindowId() ?? ''
  const rect = useCluState((s) => s.windows[windowId]?.shell.layout.pageRect)
  const tabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  const loading = useCluState((s) => (tabId ? (s.tabs[tabId]?.loading ?? false) : false))
  const progress = useCluState((s) => (tabId ? (s.tabs[tabId]?.progress ?? 0) : 0))
  const reduced = useReducedMotion()
  const live = !loading ? null : Number.isFinite(progress) && progress >= RECEIVING_AT ? 'receiving' : 'waiting'
  const [track, setTrack] = useState<LoadingTrack>(IDLE)
  // Derived during render (React's "store information from previous renders"), so the new phase and the opacity it
  // starts from reach the DOM in the same commit.
  const next = nextLoadingTrack(track, live, performance.now(), reduced)
  if (next !== track) setTrack(next)
  const done = track.phase === 'done'
  useEffect(() => {
    if (!done) return
    const timer = setTimeout(() => setTrack((t) => (t.phase === 'done' ? IDLE : t)), LOOK.loading.doneMs)
    return () => clearTimeout(timer)
  }, [done])
  if (!rect || rect.y < 7) return null
  if (track.phase === null) {
    return (
      <div className="clu-card-grabber-zone" data-shell="card-grabber-zone" aria-hidden="true" style={{ left: rect.x, width: rect.width, height: rect.y }}>
        <div className="clu-card-grabber" data-shell="card-grabber" />
      </div>
    )
  }
  const style = { left: rect.x + rect.width / 2, '--clu-loading-from': track.from.toFixed(3) } as CSSProperties
  return <div className="clu-card-loading" data-shell="card-loading" data-phase={track.phase} data-pill={track.pill || undefined} aria-hidden="true" style={style} />
}
