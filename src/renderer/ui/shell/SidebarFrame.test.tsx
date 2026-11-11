import { describe, expect, it } from 'vitest'
import { easeAt, lerpRect, type SidebarAnim } from '../../../shared/shell/tween'
import { paintSlide } from './SidebarFrame'

const from = { x: 260, y: 8, width: 1012, height: 784 }
const to = { x: 8, y: 8, width: 1264, height: 784 }
const anim: SidebarAnim = { from: 260, to: 8, startAt: 1_000_000, ms: 180, fromRect: from, toRect: to }

const px = (css: string) => Number(/(-?[\d.]+)px/.exec(css)?.[1])

function mount() {
  const frame = document.createElement('div')
  const el = document.createElement('div')
  frame.appendChild(el)
  return { frame, el }
}

describe('SidebarFrame paintSlide', () => {
  it('draws the moment it is given, any moment, the same rect the browser applies to the page view then', () => {
    const { frame, el } = mount()
    // Every 120 Hz frame of the 180 ms tween: each one a new position, the last exactly the target.
    let last = from.x + 1
    for (let i = 1; i <= 22; i++) {
      const now = anim.startAt + (i * 1000) / 120
      const done = paintSlide(el, anim, 260, now)
      const f = lerpRect(from, to, easeAt(Math.min(1, (now - anim.startAt) / anim.ms), anim.ms, 'hide'))
      // Sidebar right edge (translateX + width) is the page view's x at that moment.
      expect(px(el.style.transform)).toBeCloseTo(f.x - 260, 3)
      expect(px(frame.style.getPropertyValue('--shell-card-x'))).toBeCloseTo(f.x, 3)
      expect(px(frame.style.getPropertyValue('--shell-card-w'))).toBeCloseTo(f.width, 3)
      expect(f.x).toBeLessThan(last)
      last = f.x
      expect(done).toBe(i === 22)
    }
  })

  it('holds the start at the start and finishes at once with reduced motion', () => {
    const { el } = mount()
    expect(paintSlide(el, anim, 260, anim.startAt)).toBe(false)
    expect(el.style.transform).toBe('translateX(0px)')
    expect(paintSlide(el, { ...anim, ms: 0 }, 260, anim.startAt)).toBe(true)
    expect(el.style.transform).toBe('translateX(-252px)')
  })

  it('the Library closing keeps the narrowed sidebar at the window edge while the card slides in', () => {
    const { el } = mount()
    const close: SidebarAnim = { from: 417, to: 214, startAt: 0, ms: 290, fromRect: from, toRect: to, curve: 'exp' }
    paintSlide(el, close, 214, 32)
    expect(el.style.transform).toBe('translateX(0px)')
    const open: SidebarAnim = { ...close, from: 214, to: 417 }
    paintSlide(el, open, 417, 32)
    expect(px(el.style.transform)).toBeLessThan(0)
  })
})
