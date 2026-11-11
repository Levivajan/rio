import { act, renderHook } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import type { DownloadState } from '../../../shared/capabilities'
import type { LibraryFile } from '../../../shared/ipc/capabilities'
import { kindOf, peekItems } from './libraryModel'
import { LIBRARY_PEEK, libraryPeekEnter, libraryPeekLeave, useLibraryPeek } from './LibraryPeek'

const row = (n: number, startedAt: number): DownloadState => ({
  id: `d${n}`, profileId: 'p', filename: `f${n}.pdf`, url: 'https://a.test/', state: 'completed', receivedBytes: 1, totalBytes: 1,
  bytesPerSecond: 0, etaMs: null, startedAt, endedAt: startedAt, exists: true
})

describe('Library hover peek', () => {
  // Each timing test leaves the module's peek closed for the next.
  afterEach(() => {
    if (!vi.isFakeTimers()) return
    act(() => {
      libraryPeekLeave(true)
      vi.runAllTimers()
    })
    vi.useRealTimers()
  })

  it('lists the four newest files of downloads, the Downloads folder and Media, oldest on top, each once', () => {
    const rows = [row(1, 10), row(2, 50), row(3, 0)]
    const file = (name: string, modifiedAt: number): LibraryFile => ({ name, size: 1, modifiedAt, kind: kindOf(name) })
    const items = peekItems(rows, [file('f2.pdf', 50), file('sheet.csv', 45)], [file('shot.png', 60), file('shot.png', 60), file('old.png', 5)], LIBRARY_PEEK.rows)
    expect(items.map((i) => i.key)).toEqual(['d:d1', 'f:sheet.csv', 'd:d2', 'm:shot.png'])
    expect(peekItems([], [], [file('only.png', 1)], LIBRARY_PEEK.rows).map((i) => i.name)).toEqual(['only.png'])
    expect(peekItems([], [], [], LIBRARY_PEEK.rows)).toEqual([])
  })

  it('opens 150 ms after the pointer arrives, survives the trip to the rows, and drops after the pointer leaves', () => {
    vi.useFakeTimers()
    const { result } = renderHook(() => useLibraryPeek())
    act(() => libraryPeekEnter())
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.delayMs - 1))
    expect(result.current).toBe('closed')
    act(() => void vi.advanceTimersByTime(1))
    expect(result.current).toBe('open')
    // Button to rows: a leave, then an enter within the grace period.
    act(() => libraryPeekLeave())
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.leaveMs - 1))
    act(() => libraryPeekEnter())
    act(() => void vi.advanceTimersByTime(1000))
    expect(result.current).toBe('open')
    act(() => libraryPeekLeave())
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.leaveMs))
    expect(result.current).toBe('closing')
    act(() => void vi.advanceTimersByTime(LIBRARY_PEEK.closeMs))
    expect(result.current).toBe('closed')
  })

  it('a leave before it opens cancels it', () => {
    vi.useFakeTimers()
    const { result } = renderHook(() => useLibraryPeek())
    act(() => libraryPeekEnter())
    act(() => libraryPeekLeave())
    act(() => void vi.advanceTimersByTime(1000))
    expect(result.current).toBe('closed')
  })
})
