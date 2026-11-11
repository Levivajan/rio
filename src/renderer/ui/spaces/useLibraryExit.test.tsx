// When the Library closes, the sidebar narrows from the Library's width on the browser's Library tween
// (kLibraryMs, exponential), not in one frame, whether the narrower width lands with the close or a patch later.
import { renderHook } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { LIBRARY_TWEEN } from '../../../shared/shell/tween'
import { libraryExit, settleStart, sharedSwipe } from './swipeReducer'
import { LIBRARY_NARROW_EASING, useLibraryExit } from './useLibrarySwipe'

function navWithAnimate() {
  const el = document.createElement('nav')
  const animate = vi.fn(() => ({ finished: new Promise<void>(() => undefined), cancel: vi.fn() }) as unknown as Animation)
  el.animate = animate as unknown as HTMLElement['animate']
  return { ref: { current: el }, animate }
}

describe('useLibraryExit narrowing', () => {
  it('animates the nav width from the Library width to the sidebar width on the Library tween', () => {
    const { ref, animate } = navWithAnimate()
    const view = renderHook(({ library, width }) => useLibraryExit(ref, library, width), { initialProps: { library: true, width: 417 } })
    view.rerender({ library: false, width: 240 })
    expect(animate).toHaveBeenCalledTimes(1)
    expect(animate).toHaveBeenCalledWith([{ width: '417px' }, { width: '240px' }], { duration: LIBRARY_TWEEN.ms, easing: LIBRARY_NARROW_EASING })
  })

  it('narrows when the width arrives a patch after the close, and never on a later resize', () => {
    const { ref, animate } = navWithAnimate()
    const view = renderHook(({ library, width }) => useLibraryExit(ref, library, width), { initialProps: { library: true, width: 417 } })
    view.rerender({ library: false, width: 417 })
    expect(animate).not.toHaveBeenCalled()
    view.rerender({ library: false, width: 240 })
    expect(animate).toHaveBeenCalledWith([{ width: '417px' }, { width: '240px' }], expect.anything())
    view.rerender({ library: false, width: 200 })
    expect(animate).toHaveBeenCalledTimes(1)
  })

  it('ignores junk widths', () => {
    const { ref, animate } = navWithAnimate()
    const view = renderHook(({ library, width }) => useLibraryExit(ref, library, width), { initialProps: { library: true, width: Number.NaN } })
    view.rerender({ library: false, width: 240 })
    expect(animate).not.toHaveBeenCalled()
  })

  it('picks the Library exit up where it is going to be this frame, one frame in, never held', () => {
    const { ref, animate } = navWithAnimate()
    const anims: Animation[] = []
    animate.mockImplementation(() => {
      const a = { finished: new Promise<void>(() => undefined), cancel: vi.fn(), currentTime: 0 } as unknown as Animation
      anims.push(a)
      return a
    })
    const view = renderHook(({ library }) => useLibraryExit(ref, library), { initialProps: { library: true } })
    // A slow swipe released the Library 260 px out of 418 at 0.33 px/ms, one frame ago.
    const now = performance.now()
    sharedSwipe.exit = libraryExit(260, 418, 0.33, now - 1000 / 60)
    view.rerender({ library: false })
    const exit = view.result.current
    expect(exit).not.toBeNull()
    expect(animate).toHaveBeenCalledWith([{ transform: `translateX(${418 - 260}px)` }, { transform: 'translateX(0)' }], expect.objectContaining({ duration: exit?.ms }))
    // Two frames into the curve: the frame it closes in already moves on from the Library's last frame.
    const t = Number(anims[0]?.currentTime)
    expect(t).toBeGreaterThanOrEqual(2 * (1000 / 60) - 0.01)
    expect(t).toBeLessThanOrEqual(settleStart(exit?.at, performance.now(), exit?.ms ?? 0))
    sharedSwipe.exit = null
  })

  it('settleStart starts one frame in and survives junk', () => {
    expect(settleStart(100, 100, 300)).toBeCloseTo(1000 / 60)
    expect(settleStart(100, 150, 300)).toBeCloseTo(50 + 1000 / 60)
    expect(settleStart(100, 1000, 300)).toBe(300)
    expect(settleStart(undefined, 100, 300)).toBeCloseTo(1000 / 60)
    expect(settleStart(Number.NaN, 100, 300)).toBeCloseTo(1000 / 60)
    expect(settleStart(100, 50, 300)).toBeCloseTo(1000 / 60)
    expect(settleStart(0, 100, Number.NaN)).toBe(0)
  })
})
