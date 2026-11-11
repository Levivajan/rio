// Sidebar wrapper slot (docs/plan/01-shell.md F3 and F11): slides the sidebar with the page card. Main
// tweens the page view on 16 ms ticks from windows[id].shell.sidebar.anim.startAt; this frame draws the same tick
// (edgeAt), so the sidebar's right edge and the card's left edge move together. The card placeholder follows too.
import { useLayoutEffect, useRef, type ReactNode } from 'react'
import { lerp, lerpRect, progressAt, type SidebarAnim } from '../../../shared/shell/tween'
import { useCluState, useWindowId } from '../../common/useCluState'
import './shell.css'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'

const CARD_VARS = ['--shell-card-x', '--shell-card-y', '--shell-card-w', '--shell-card-h'] as const

/** Draws one moment of the slide: the sidebar offset and the card rect at `now`. True once the slide has ended. */
export function paintSlide(el: HTMLElement, anim: SidebarAnim, width: number, now: number): boolean {
  const p = progressAt(anim, now)
  // The Library closing narrows the sidebar under the tab list: its left edge stays put while the card's slides in,
  // never past the window's left edge.
  el.style.transform = `translateX(${Math.min(0, lerp(anim.from, anim.to, p) - width)}px)`
  el.style.visibility = 'visible'
  const card = lerpRect(anim.fromRect, anim.toRect, p)
  const frame = el.parentElement
  if (frame) {
    frame.dataset.shellTween = 'true'
    ;[card.x, card.y, card.width, card.height].forEach((v, i) => frame.style.setProperty(CARD_VARS[i] as string, `${v}px`))
  }
  return p >= 1
}

function paintRest(el: HTMLElement, shown: boolean, width: number): void {
  el.style.transform = shown ? '' : `translateX(${-width}px)`
  el.style.visibility = shown ? '' : 'hidden'
  const frame = el.parentElement
  if (frame) {
    delete frame.dataset.shellTween
    for (const v of CARD_VARS) frame.style.removeProperty(v)
  }
}

export function SidebarFrame({ children }: { children: ReactNode }) {
  const windowId = useWindowId() ?? ''
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  const shown = useCluState((s) => {
    const shell = s.windows[windowId]?.shell
    return shell ? !shell.sidebar.hidden && !shell.focusMode : true
  })
  const focusMode = useCluState((s) => s.windows[windowId]?.shell.focusMode ?? false)
  const anim = useCluState((s) => s.windows[windowId]?.shell.sidebar.anim ?? null)
  const ref = useRef<HTMLDivElement>(null)

  useLayoutEffect(() => {
    const el = ref.current
    if (!el) return
    if (!anim) {
      paintRest(el, shown, width)
      return
    }
    let raf = 0
    const step = () => {
      if (paintSlide(el, anim, width, Date.now())) paintRest(el, shown, width)
      else raf = requestAnimationFrame(step)
    }
    step()
    return () => cancelAnimationFrame(raf)
  }, [anim, shown, width])

  return (
    <div ref={ref} className="clu-sidebar-frame" data-shell="sidebar-frame" data-hidden={String(!shown)} data-focus={String(focusMode)} style={{ width }} inert={!shown && !anim}>
      {children}
    </div>
  )
}
