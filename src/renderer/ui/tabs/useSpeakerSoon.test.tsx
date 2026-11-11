import { act, renderHook } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { SPEAKER_IN_MS, useSpeakerSoon } from './useSpeakerSoon'
import type { RowView } from './rowModel'

describe('useSpeakerSoon', () => {
  beforeEach(() => vi.useFakeTimers())
  afterEach(() => vi.useRealTimers())

  it('shows the speaker only once a tab has played for 0.8 s', () => {
    const { result, rerender } = renderHook(({ audio }: { audio: RowView['audio'] }) => useSpeakerSoon(audio), { initialProps: { audio: null as RowView['audio'] } })
    rerender({ audio: 'playing' })
    expect(result.current).toBe(null)
    act(() => vi.advanceTimersByTime(SPEAKER_IN_MS - 1))
    expect(result.current).toBe(null)
    act(() => vi.advanceTimersByTime(1))
    expect(result.current).toBe('playing')
    expect(SPEAKER_IN_MS).toBe(800)
  })

  it('a short sound never shows it; muted and stopped show at once', () => {
    const { result, rerender } = renderHook(({ audio }: { audio: RowView['audio'] }) => useSpeakerSoon(audio), { initialProps: { audio: null as RowView['audio'] } })
    rerender({ audio: 'playing' })
    act(() => vi.advanceTimersByTime(300))
    rerender({ audio: null })
    act(() => vi.advanceTimersByTime(SPEAKER_IN_MS))
    expect(result.current).toBe(null)
    rerender({ audio: 'muted' })
    expect(result.current).toBe('muted')
    rerender({ audio: 'playing' })
    expect(result.current).toBe(null)
    act(() => vi.advanceTimersByTime(SPEAKER_IN_MS))
    expect(result.current).toBe('playing')
    rerender({ audio: null })
    expect(result.current).toBe(null)
  })

  it('a tab that already plays when the row mounts waits too', () => {
    const { result } = renderHook(() => useSpeakerSoon('playing'))
    expect(result.current).toBe(null)
    act(() => vi.advanceTimersByTime(SPEAKER_IN_MS))
    expect(result.current).toBe('playing')
  })
})
