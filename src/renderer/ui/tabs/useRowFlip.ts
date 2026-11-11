// Moves the tab list's rows to their new places (rowFlip.ts): after each render it
// compares every row's layout position with the last one and slides the rows that moved from where they were. Rows
// that are new just appear; a row the drop FLIP animates keeps that animation; nothing moves during a drag or with
// reduced motion. A render that moves nothing (a state echo) starts nothing, and a row that moves again mid-slide
// continues from where it is drawn instead of jumping. A closed or unpinned tab fades where it was while the rows
// below close up. A folder that collapses slides its children up under its row while the rows below close up:
// copies of the gone rows, clipped at the folder row's bottom edge; other gone rows just go.
import { useLayoutEffect, useRef, type RefObject } from 'react'
import { CLEAR_FADE, FOLDER_COLLAPSE, flipTiming, MAX_FLIP_PX, ROW_FADE, ROW_MOVE, type FlipTiming, type ListShape } from './rowFlip'
import { sidebarDrag } from './useSidebarDrag'

const ITEMS = '[role="treeitem"], .clu-today-divider'

function keyOf(el: HTMLElement): string {
  const d = el.dataset
  if (d.tabId) return `t:${d.tabId}`
  if (d.folderId) return `f:${d.folderId}`
  if (d.splitId) return `s:${d.splitId}`
  if (d.newTab) return 'new-tab'
  return el.classList.contains('clu-today-divider') ? 'divider' : ''
}

interface Place {
  el: HTMLElement
  top: number
  left: number
  width: number
  height: number
}

/** The children `folderKey` hid: rows gone from the list that sat between the folder and the next row still there. */
function hiddenUnder(folderKey: string, before: Map<string, Place>, now: Map<string, number>): Place[] {
  const folder = before.get(folderKey)
  if (!folder) return []
  let next = Infinity
  for (const [key, p] of before) if (now.has(key) && p.top > folder.top) next = Math.min(next, p.top)
  const out: Place[] = []
  for (const [key, p] of before) if (!now.has(key) && !p.el.isConnected && p.top > folder.top && p.top < next) out.push(p)
  return out.sort((a, b) => a.top - b.top)
}

/** Slides copies of `rows` up under the folder row at `under` (its new bottom edge) and drops them at the end. */
function slideUnder(root: HTMLElement, under: number, rows: Place[], timing: FlipTiming): void {
  const last = rows[rows.length - 1]
  if (!last) return
  const depth = last.top + last.height - under
  if (depth <= 0) return
  const clip = document.createElement('div')
  clip.className = 'clu-row-collapse-clip'
  clip.setAttribute('aria-hidden', 'true')
  clip.inert = true
  clip.style.top = `${under}px`
  clip.style.height = `${depth}px`
  const inner = document.createElement('div')
  for (const p of rows) {
    const copy = p.el.cloneNode(true) as HTMLElement
    for (const name of ['role', 'id', 'tabindex', 'aria-level', 'aria-selected', 'aria-expanded', 'data-tab-id', 'data-folder-id', 'data-split-id']) copy.removeAttribute(name)
    Object.assign(copy.style, { position: 'absolute', margin: '0', top: `${p.top - under}px`, left: `${p.left}px`, width: `${p.width}px`, height: `${p.height}px` })
    // The title and the close button go in the first frame; only the favicon slides up.
    for (const part of copy.querySelectorAll<HTMLElement>('.clu-row-title, .clu-row-title-stack, .clu-row-close')) part.style.visibility = 'hidden'
    inner.append(copy)
  }
  clip.append(inner)
  // First in the list, so the folder row and the rows closing up paint over it.
  root.prepend(clip)
  const anim = inner.animate([{ transform: 'none' }, { transform: `translateY(${-depth}px)` }], { duration: timing.ms, easing: timing.easing })
  const done = () => clip.remove()
  anim.addEventListener('finish', done)
  anim.addEventListener('cancel', done)
}

/** The id of this hook's slides, so a row moved again mid-slide restarts from where it is (the drop FLIP's are not). */
export const ROW_FLIP_ID = 'clu-row-flip'

/** The vertical offset a running transform draws `el` at, 0 at rest. */
function drawnY(el: HTMLElement): number {
  const m = /matrix(3d)?\(([^)]+)\)/.exec(getComputedStyle(el).transform)
  if (!m?.[2]) return 0
  const v = m[2].split(',').map(Number)
  const y = m[1] ? v[13] : v[5]
  return y !== undefined && Number.isFinite(y) ? y : 0
}

/** Fades copies of the tab rows that left the list where they were; the rows closing up paint over them. */
function fadeGone(root: HTMLElement, rows: Place[], timing: FlipTiming): void {
  for (const p of rows) {
    const copy = p.el.cloneNode(true) as HTMLElement
    for (const name of ['role', 'id', 'tabindex', 'aria-level', 'aria-selected', 'aria-expanded', 'data-tab-id', 'data-folder-id', 'data-split-id']) copy.removeAttribute(name)
    copy.setAttribute('aria-hidden', 'true')
    copy.inert = true
    copy.classList.add('clu-row-gone')
    // The divider names the anchor Clear sits on; a copy must not take it over.
    copy.style.setProperty('anchor-name', 'none')
    Object.assign(copy.style, { top: `${p.top}px`, left: `${p.left}px`, width: `${p.width}px`, height: `${p.height}px` })
    root.prepend(copy)
    const anim = copy.animate([{ opacity: 1 }, { opacity: 0 }], { duration: timing.ms, easing: timing.easing, fill: 'forwards' })
    const done = () => copy.remove()
    anim.addEventListener('finish', done)
    anim.addEventListener('cancel', done)
  }
}

export function useRowFlip(list: RefObject<HTMLElement | null>, shape: ListShape): void {
  const last = useRef<{ tops: Map<string, number>; places: Map<string, Place>; shape: ListShape } | null>(null)
  useLayoutEffect(() => {
    const root = list.current
    if (!root) return
    const tops = new Map<string, number>()
    const places = new Map<string, Place>()
    const els: [string, HTMLElement][] = []
    for (const el of root.querySelectorAll<HTMLElement>(ITEMS)) {
      const key = keyOf(el)
      if (!key || tops.has(key)) continue
      // offsetTop is the layout place, so a running slide does not skew the next one.
      tops.set(key, el.offsetTop)
      places.set(key, { el, top: el.offsetTop, left: el.offsetLeft, width: el.offsetWidth, height: el.offsetHeight })
      els.push([key, el])
    }
    const before = last.current
    last.current = { tops, places, shape }
    if (!before || sidebarDrag.get().dragging || document.documentElement.dataset.reducedMotion === 'true') return
    const timing = flipTiming(before.shape, shape)
    if (timing === FOLDER_COLLAPSE && typeof Element.prototype.animate === 'function') {
      for (const [id, collapsed] of Object.entries(shape.folders)) {
        if (!collapsed || before.shape.folders[id] !== false) continue
        const row = places.get(`f:${id}`)
        if (row) slideUnder(root, row.top + row.height, hiddenUnder(`f:${id}`, before.places, tops), timing)
      }
    }
    if (timing === ROW_MOVE && typeof Element.prototype.animate === 'function') {
      const left = (key: string, p: Place) => !tops.has(key) && !p.el.isConnected
      if (root.querySelector('[data-clearing]')) {
        // Clear: its rows are already hidden; the divider fades as New Tab slides up over it.
        const divider = before.places.get('divider')
        if (divider && left('divider', divider)) fadeGone(root, [divider], CLEAR_FADE)
      } else {
        const gone = [...before.places].filter(([key, p]) => key.startsWith('t:') && left(key, p)).map(([, p]) => p)
        // A whole list that went at once is not a close.
        if (gone.length > 0 && gone.length < before.places.size) fadeGone(root, gone, ROW_FADE)
      }
    }
    for (const [key, el] of els) {
      const was = before.tops.get(key)
      const now = tops.get(key)
      if (was === undefined || now === undefined || typeof el.animate !== 'function') continue
      const dy = was - now
      if (dy === 0 || Math.abs(dy) > MAX_FLIP_PX) continue
      const running = el.getAnimations().filter((a) => a.playState === 'running')
      if (running.some((a) => a.id !== ROW_FLIP_ID)) continue
      // Interrupted: start from where the row is drawn now, not from its last resting place.
      const from = dy + (running.length > 0 ? drawnY(el) : 0)
      for (const a of running) a.cancel()
      el.animate([{ transform: `translateY(${from}px)` }, { transform: 'none' }], { id: ROW_FLIP_ID, duration: timing.ms, easing: timing.easing })
    }
  })
}
