import { LOOK } from '../../../shared/look/tokens'
import { clu } from '../../common/clu'
// Row hover card timing (02-tabs.md F15 and Timings): the card shows after 1.5 s with the pointer on a row
// (M11), hung from the pointer, which the anchor carries as a zero-size rect. Moving on to the next row hides it
// and the next card waits the full delay again. Pointer leave, a press,
// a right-click, scrolling and drag start hide it; main places and draws it (tabs.hover, surface tab-hover).
// Gmail's card is usable: the pointer leaving its tile or row closes it only after
// HOVER_HOLD_MS, time to cross onto the card, which then holds itself open (tabs.hover held) until the pointer leaves it.

export const HOVER_DELAY_MS = LOOK.tooltip.hoverCardDelayMs
/** How long a card that holds (Gmail's) stays after the pointer leaves its tile. */
export const HOVER_HOLD_MS = 75

let timer: ReturnType<typeof setTimeout> | null = null
let shown: string | null = null
let pointer: { x: number; y: number } | null = null
let holds: string | null = null
let leaving: ReturnType<typeof setTimeout> | null = null

const stopLeaving = () => {
  if (leaving) clearTimeout(leaving)
  leaving = null
}

/** The pointer's latest position over the row, where the card will hang from. */
export function hoverMove(x: number, y: number): void {
  pointer = { x, y }
}

/** `delayMs`: a favorite's preview card waits less than a row's title card (MAIL_PREVIEW). `hold`: the card can be
 *  reached with the pointer (Gmail's). */
export function hoverEnter(tabId: string, el: HTMLElement, at?: { x: number; y: number }, delayMs: number = HOVER_DELAY_MS, hold = false): void {
  if (timer) clearTimeout(timer)
  timer = null
  const wasLeaving = leaving !== null
  stopLeaving()
  if (at) pointer = at
  // Back on the tile whose card was about to close: it stays. A card that holds may have released its own hold and
  // started to fade in the meantime (the pointer crossed it and came back), so it is asked for again.
  if (wasLeaving && shown === tabId) {
    if (holds === tabId) void clu().tabs.hover({ tabId, anchor: anchorAt(el) })
    return
  }
  holds = hold ? tabId : null
  // The card belongs to the tile or row under the pointer; another one's card closes now, not when the new
  // one is due.
  if (shown !== null && shown !== tabId) {
    shown = null
    void clu().tabs.hover({ tabId: null })
  }
  timer = setTimeout(() => {
    timer = null
    if (!el.isConnected) return
    shown = tabId
    void clu().tabs.hover({ tabId, anchor: anchorAt(el) })
  }, delayMs)
}

/** Where the card hangs from: the pointer, or without one (a synthetic enter) the row's middle. */
function anchorAt(el: HTMLElement): { x: number; y: number; width: number; height: number } {
  const r = el.getBoundingClientRect()
  const p = pointer ?? { x: r.x + r.width / 2, y: r.y + r.height / 2 }
  return { x: Math.round(p.x), y: Math.round(p.y), width: 0, height: 0 }
}

/** Closes the card now (a press, a right-click, scrolling, a drag). */
export function hoverLeave(): void {
  if (timer) clearTimeout(timer)
  timer = null
  stopLeaving()
  pointer = null
  if (shown === null) return
  shown = null
  void clu().tabs.hover({ tabId: null })
}

/** The pointer left the tile or row: a card that holds closes after HOVER_HOLD_MS unless the pointer reached it. */
export function hoverPointerLeave(): void {
  if (shown === null || shown !== holds) {
    hoverLeave()
    return
  }
  if (timer) clearTimeout(timer)
  timer = null
  stopLeaving()
  leaving = setTimeout(hoverLeave, HOVER_HOLD_MS)
}
