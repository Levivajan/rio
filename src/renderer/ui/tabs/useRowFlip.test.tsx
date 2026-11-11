import { render } from '@testing-library/react'
import { useRef } from 'react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { CLEAR_FADE, ROW_FADE, ROW_MOVE } from './rowFlip'
import { ROW_FLIP_ID, useRowFlip } from './useRowFlip'

interface FakeAnim {
  id: string
  playState: string
  keyframes: Keyframe[]
  options: KeyframeAnimationOptions
  cancel: () => void
  addEventListener: () => void
}

const anims = new Map<Element, FakeAnim[]>()

beforeEach(() => {
  anims.clear()
  Object.defineProperty(HTMLElement.prototype, 'offsetTop', { configurable: true, get() { return Number((this as HTMLElement).dataset.top ?? 0) } })
  Element.prototype.animate = vi.fn(function (this: Element, keyframes: Keyframe[], options: KeyframeAnimationOptions) {
    const a: FakeAnim = { id: options.id ?? '', playState: 'running', keyframes, options, cancel: () => (a.playState = 'idle'), addEventListener: () => {} }
    anims.set(this, [...(anims.get(this) ?? []), a])
    return a as unknown as Animation
  }) as unknown as Element['animate']
  Element.prototype.getAnimations = function (this: Element) {
    return (anims.get(this) ?? []).filter((a) => a.playState === 'running') as unknown as Animation[]
  }
})

afterEach(() => {
  delete (HTMLElement.prototype as { offsetTop?: number }).offsetTop
})

type Row = { id: string; top: number }

function List({ rows, divider, clearing }: { rows: Row[]; divider?: number; clearing?: boolean }) {
  const ref = useRef<HTMLDivElement>(null)
  useRowFlip(ref, { folders: {}, pinnedCollapsed: false })
  return (
    <div ref={ref}>
      <div className="clu-tab-tree" data-clearing={clearing ? '' : undefined}>
        {divider !== undefined && <div className="clu-today-divider" data-top={divider} />}
        {rows.map((r) => (
          <div key={r.id} role="treeitem" data-tab-id={r.id} data-top={r.top} />
        ))}
      </div>
    </div>
  )
}

function must<T>(v: T | null | undefined): T {
  if (v === null || v === undefined) throw new Error('missing')
  return v
}
const rowEl = (c: HTMLElement, id: string) => must(c.querySelector<HTMLElement>(`[data-tab-id="${id}"]`))
const slides = (el: Element) => (anims.get(el) ?? []).filter((a) => a.id === ROW_FLIP_ID)

describe('useRowFlip (motion.md: pinning, unpinning, Clear, folders, drag and drop)', () => {
  it('slides a moved row once per move in 110 ms, and a re-render that moves nothing starts nothing', () => {
    const { container, rerender } = render(<List rows={[{ id: 'a', top: 0 }, { id: 'b', top: 41 }]} />)
    rerender(<List rows={[{ id: 'a', top: 0 }, { id: 'b', top: 41 }]} />)
    expect(Element.prototype.animate).not.toHaveBeenCalled()
    rerender(<List rows={[{ id: 'b', top: 0 }, { id: 'a', top: 41 }]} />)
    const b = slides(rowEl(container, 'b'))
    expect(b).toHaveLength(1)
    expect(b[0]?.keyframes[0]).toEqual({ transform: 'translateY(41px)' })
    expect(b[0]?.options.duration).toBe(ROW_MOVE.ms)
    // The state echo of the same move.
    rerender(<List rows={[{ id: 'b', top: 0 }, { id: 'a', top: 41 }]} />)
    rerender(<List rows={[{ id: 'b', top: 0 }, { id: 'a', top: 41 }]} />)
    expect(slides(rowEl(container, 'b'))).toHaveLength(1)
  })

  it('continues an interrupted slide from where the row is drawn instead of jumping', () => {
    const { container, rerender } = render(<List rows={[{ id: 'a', top: 0 }, { id: 'b', top: 82 }]} />)
    rerender(<List rows={[{ id: 'a', top: 0 }, { id: 'b', top: 41 }]} />)
    const b = rowEl(container, 'b')
    const first = must(slides(b)[0])
    // Halfway through its 41 pt slide the row is drawn 20 pt below its new place, then it moves up 41 again.
    b.style.transform = 'matrix(1, 0, 0, 1, 0, 20)'
    rerender(<List rows={[{ id: 'b', top: 0 }, { id: 'a', top: 41 }]} />)
    expect(first.playState).toBe('idle')
    const next = must(slides(b)[1])
    expect(next.keyframes[0]).toEqual({ transform: 'translateY(61px)' })
  })

  it('fades a closed or unpinned tab where it was while the rows below close up', () => {
    const { container, rerender } = render(<List rows={[{ id: 'a', top: 0 }, { id: 'b', top: 41 }, { id: 'c', top: 82 }]} />)
    rerender(<List rows={[{ id: 'a', top: 0 }, { id: 'c', top: 41 }]} />)
    const gone = must(container.querySelector<HTMLElement>('.clu-row-gone'))
    expect(gone.style.top).toBe('41px')
    expect(gone.hasAttribute('data-tab-id')).toBe(false)
    const fade = must(anims.get(gone)?.[0])
    expect(fade.keyframes).toEqual([{ opacity: 1 }, { opacity: 0 }])
    expect(fade.options.duration).toBe(ROW_FADE.ms)
    expect(slides(rowEl(container, 'c'))[0]?.keyframes[0]).toEqual({ transform: 'translateY(41px)' })
  })

  it('does not fade copies when every row went at once (Clear already faded them)', () => {
    const { container, rerender } = render(<List rows={[{ id: 'a', top: 0 }, { id: 'b', top: 41 }]} />)
    rerender(<List rows={[]} />)
    expect(container.querySelector('.clu-row-gone')).toBeNull()
  })

  it('on Clear fades only the divider, in 50 ms, while New Tab slides up over it', () => {
    const { container, rerender } = render(<List rows={[{ id: 'p', top: 0 }, { id: 'a', top: 66 }, { id: 'b', top: 107 }]} divider={41} />)
    rerender(<List rows={[{ id: 'p', top: 0 }, { id: 'a', top: 66 }, { id: 'b', top: 107 }]} divider={41} clearing />)
    rerender(<List rows={[{ id: 'p', top: 0 }]} clearing />)
    const gone = [...container.querySelectorAll<HTMLElement>('.clu-row-gone')]
    expect(gone.map((el) => el.className)).toEqual(['clu-today-divider clu-row-gone'])
    expect(must(anims.get(must(gone[0]))?.[0]).options.duration).toBe(CLEAR_FADE.ms)
  })
})
