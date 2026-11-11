// Zap picker (docs/plan/08-extensions.md "Picker", docs/chromium/m7.md). build-webui.mjs bundles entry.ts
// into page/zap.js, which the browser injects into the page's isolated world (CluZapSession, with the update and pick
// calls appended): a closed-shadow-root host at max z-index holds the highlight box and the zap bar. Capture listeners
// on window swallow pointer input so page handlers never fire, untrusted events are ignored, and each `pick()`
// resolves once with the next result. Like selector.ts, `zapPicker` must stay self-contained.
import type { ZapTarget } from './selector'
import { zapTarget } from './selector'

/** What the bar shows: the site's hidden count and the last result line. */
export interface ZapBar { count: number; message: string | null }

export type PickResult =
  | { kind: 'zap'; selector: string; label: string; related: boolean; matches: number }
  | { kind: 'undo' }
  | { kind: 'done' }
  | { kind: 'stopped' }

interface ZapApi { pick(): Promise<PickResult>; update(bar: ZapBar): void; stop(): void }

export function zapPicker(target: (el: Element) => ZapTarget | null): void {
  const g = globalThis as unknown as { __cluZap?: ZapApi; __cluZapStop?: () => void }
  if (g.__cluZap) return
  const doc = document
  const ROSE = '#f43f5e'
  const host = doc.createElement('div')
  const hostStyle: Record<string, string> = {
    all: 'initial', position: 'fixed', inset: '0', 'z-index': '2147483647', 'pointer-events': 'none', display: 'block', margin: '0', padding: '0', border: '0', background: 'transparent'
  }
  for (const [k, v] of Object.entries(hostStyle)) host.style.setProperty(k, v, 'important')
  const shadow = host.attachShadow({ mode: 'closed' })
  // A constructed sheet, so a page CSP that forbids inline styles cannot strip the bar.
  const sheet = new CSSStyleSheet()
  sheet.replaceSync(`
    :host { all: initial; }
    * { box-sizing: border-box; }
    .box { position: fixed; display: none; pointer-events: none; border-radius: 3px; outline: 2px solid ${ROSE}; background: rgb(244 63 94 / 10%); }
    .tag { position: absolute; left: -2px; top: -22px; padding: 0 6px; border-radius: 4px; background: ${ROSE}; color: #fff; font: 600 11px/18px -apple-system, BlinkMacSystemFont, system-ui, sans-serif; white-space: nowrap; max-width: 320px; overflow: hidden; text-overflow: ellipsis; }
    .tag.below { top: auto; bottom: -22px; }
    .bar { position: fixed; left: 50%; bottom: 16px; transform: translateX(-50%); display: flex; align-items: center; gap: 10px; max-width: calc(100vw - 32px);
      padding: 6px 6px 6px 14px; border-radius: 12px; background: rgb(28 28 30 / 94%); color: #f2f2f7; pointer-events: auto; cursor: default; user-select: none;
      font: 13px/20px -apple-system, BlinkMacSystemFont, system-ui, sans-serif; box-shadow: 0 8px 24px rgb(0 0 0 / 30%); }
    .title { font-weight: 600; white-space: nowrap; }
    .msg { color: #c7c7cc; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-width: 0; }
    .count { color: #fff; font-variant-numeric: tabular-nums; white-space: nowrap; }
    button { all: unset; padding: 4px 12px; border-radius: 8px; background: rgb(255 255 255 / 14%); color: #fff; font: 500 13px/20px -apple-system, BlinkMacSystemFont, system-ui, sans-serif; white-space: nowrap; }
    button:hover { background: rgb(255 255 255 / 22%); }
    button.done { background: ${ROSE}; }
    button.done:hover { background: #e11d48; }
  `)
  shadow.adoptedStyleSheets = [sheet]
  const el = (tag: string, cls: string, text = ''): HTMLElement => {
    const e = doc.createElement(tag)
    e.className = cls
    e.textContent = text
    return e
  }
  const box = el('div', 'box')
  const tag = el('div', 'tag')
  box.append(tag)
  const bar = el('div', 'bar')
  bar.setAttribute('role', 'toolbar')
  bar.setAttribute('aria-label', 'Zap mode')
  const hint = 'Click to hide · Shift+click hides similar'
  const msg = el('span', 'msg', hint)
  const count = el('span', 'count', '0 hidden')
  const undo = el('button', 'undo', 'Undo')
  const done = el('button', 'done', 'Done')
  bar.append(el('span', 'title', 'Zap mode'), msg, count, undo, done)
  shadow.append(box, bar)

  const queue: PickResult[] = []
  let waiting: ((r: PickResult) => void) | null = null
  const emit = (r: PickResult): void => {
    if (waiting) {
      const w = waiting
      waiting = null
      w(r)
    } else if (queue.length < 8) queue.push(r)
  }

  let current: Element | null = null
  let trail: Element[] = []
  let lastX = -1
  let lastY = -1
  let frame = 0
  const skip = (e: Element | null): boolean => !e || e === host || e === doc.documentElement || e === doc.body
  const draw = (): void => {
    if (!current || !current.isConnected) {
      box.style.display = 'none'
      return
    }
    const r = current.getBoundingClientRect()
    if (r.width === 0 && r.height === 0) {
      box.style.display = 'none'
      return
    }
    Object.assign(box.style, { display: 'block', left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`, height: `${r.height}px` })
    const id = current.getAttribute('id')
    const cls = (current.getAttribute('class') ?? '').trim().split(/\s+/)[0]
    tag.textContent = `${current.localName}${id ? `#${id}` : cls ? `.${cls}` : ''}`.slice(0, 80)
    tag.className = r.top < 24 ? 'tag below' : 'tag'
  }
  const hover = (): void => {
    frame = 0
    const at = doc.elementFromPoint(lastX, lastY)
    if (skip(at)) {
      current = null
      trail = []
    } else if (at !== current && !(current && trail.length > 0 && current.contains(at))) {
      current = at
      trail = []
    }
    draw()
  }
  const onMove = (e: MouseEvent): void => {
    if (!e.isTrusted) return
    lastX = e.clientX
    lastY = e.clientY
    if (!frame) frame = requestAnimationFrame(hover)
  }
  const onPointer = (e: MouseEvent): void => {
    if (!e.isTrusted) return
    e.preventDefault()
    e.stopImmediatePropagation()
    if (e.type !== 'click') return
    if (e.target === host) {
      const b = shadow.elementFromPoint(e.clientX, e.clientY)?.closest('button')
      if (b === undo) emit({ kind: 'undo' })
      else if (b === done) emit({ kind: 'done' })
      return
    }
    const at = doc.elementFromPoint(e.clientX, e.clientY)
    if (skip(at)) return
    const pickEl = current && current.contains(at) ? current : (at as Element)
    const t = target(pickEl)
    if (!t) {
      msg.textContent = 'Rio can’t hide this element'
      return
    }
    const related = e.shiftKey && t.related !== t.selector
    const selector = related ? t.related : t.selector
    let matches: number
    try {
      matches = Math.min(doc.querySelectorAll(selector).length, 1_000_000)
    } catch {
      matches = 1
    }
    current = null
    trail = []
    draw()
    emit({ kind: 'zap', selector, label: t.label, related, matches })
  }
  const onKey = (e: KeyboardEvent): void => {
    if (!e.isTrusted) return
    const plain = !e.metaKey && !e.ctrlKey && !e.altKey && !e.shiftKey
    if (e.key === 'Escape' && plain) emit({ kind: 'done' })
    else if (e.code === 'KeyZ' && e.metaKey && !e.ctrlKey && !e.altKey && !e.shiftKey) emit({ kind: 'undo' })
    else if (e.key === 'ArrowUp' && plain) {
      const up = current?.parentElement ?? null
      if (current && up && !skip(up)) {
        trail.push(current)
        current = up
      }
      draw()
    } else if (e.key === 'ArrowDown' && plain) {
      current = trail.pop() ?? current
      draw()
    } else return
    e.preventDefault()
    e.stopImmediatePropagation()
  }
  const onScroll = (): void => {
    if (!frame) frame = requestAnimationFrame(draw)
  }

  const POINTER = ['click', 'mousedown', 'mouseup', 'pointerdown', 'pointerup', 'dblclick', 'auxclick', 'contextmenu'] as const
  for (const t of POINTER) window.addEventListener(t, onPointer, true)
  window.addEventListener('mousemove', onMove, true)
  window.addEventListener('keydown', onKey, true)
  window.addEventListener('scroll', onScroll, true)
  doc.documentElement.append(host)
  // A page that removes the host gets it back.
  const watch = new MutationObserver(() => {
    if (!host.isConnected && doc.documentElement) doc.documentElement.append(host)
  })
  watch.observe(doc, { childList: true, subtree: true })

  const stop = (): void => {
    for (const t of POINTER) window.removeEventListener(t, onPointer, true)
    window.removeEventListener('mousemove', onMove, true)
    window.removeEventListener('keydown', onKey, true)
    window.removeEventListener('scroll', onScroll, true)
    watch.disconnect()
    if (frame) cancelAnimationFrame(frame)
    host.remove()
    delete g.__cluZap
    delete g.__cluZapStop
    emit({ kind: 'stopped' })
    queue.length = 0
  }
  g.__cluZap = {
    pick: () =>
      new Promise<PickResult>((resolve) => {
        const next = queue.shift()
        if (next) resolve(next)
        else {
          waiting?.({ kind: 'stopped' })
          waiting = resolve
        }
      }),
    update(b) {
      const n = Number.isFinite(b.count) ? Math.max(0, Math.floor(b.count)) : 0
      count.textContent = `${n} hidden`
      msg.textContent = typeof b.message === 'string' && b.message ? b.message.slice(0, 120) : hint
    },
    stop
  }
  g.__cluZapStop = stop
}

/** Installs the picker when needed, refreshes the bar, then waits for the next pick (the tests run it as page text;
 * the browser builds the same call from page/zap.js). */
export const pickScript = (bar: ZapBar): string =>
  `(${zapPicker.toString()})(${zapTarget.toString()});\nglobalThis.__cluZap.update(${JSON.stringify(bar)});\nglobalThis.__cluZap.pick()`

// The browser sends these two as they are (kZapUpdateScript and kZapStopScript in clu_zap_session.cc).
export const updateScript = (bar: ZapBar): string => `globalThis.__cluZap && globalThis.__cluZap.update(${JSON.stringify(bar)});\nvoid 0`

export const STOP_SCRIPT = `typeof globalThis.__cluZapStop === 'function' && globalThis.__cluZapStop();\nvoid 0`
