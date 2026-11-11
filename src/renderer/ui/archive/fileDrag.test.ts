import { afterEach, describe, expect, it, vi } from 'vitest'
import type { DragEvent } from 'react'
import type { DownloadState } from '../../../shared/state/capabilities'
import { stubClu } from '../../../test-utils/stubClu'
import { dragTargetOf, startFileDrag } from './fileDrag'
import type { LibraryItem } from './libraryModel'

// A Library file drags out as the file, by download id or by folder and name, and nothing else drags.
const D = '00000000-0000-4000-8000-000600000001'
const download = (patch: Partial<DownloadState>): LibraryItem => ({
  key: `d:${D}`,
  type: 'download',
  name: 'report.pdf',
  at: 1,
  kind: 'file',
  download: {
    id: D,
    profileId: '00000000-0000-4000-8000-000200000001',
    filename: 'report.pdf',
    url: 'https://example.com/report.pdf',
    state: 'completed',
    receivedBytes: 1,
    totalBytes: 1,
    bytesPerSecond: 0,
    etaMs: null,
    startedAt: 1,
    endedAt: 2,
    exists: true,
    ...patch
  }
})
const shot: LibraryItem = { key: 'm:a.png', type: 'file', name: 'a.png', at: 1, kind: 'image', source: 'media', file: { name: 'a.png', size: 1, modifiedAt: 1, kind: 'image' } }
const dragEvent = () => ({ preventDefault: vi.fn() }) as unknown as DragEvent & { preventDefault: ReturnType<typeof vi.fn> }

describe('fileDrag', () => {
  afterEach(() => {
    delete (window as { clu?: unknown }).clu
  })

  it('drags a finished download by id and a folder file by source and name, never a running, failed or removed one', () => {
    expect(dragTargetOf(download({}))).toEqual({ downloadId: D })
    expect(dragTargetOf(shot)).toEqual({ source: 'media', name: 'a.png' })
    expect(dragTargetOf(download({ state: 'progressing', endedAt: null }))).toBeNull()
    expect(dragTargetOf(download({ state: 'interrupted' }))).toBeNull()
    expect(dragTargetOf(download({ exists: false }))).toBeNull()
  })

  it('cancels the HTML drag and asks the browser for the native file drag', async () => {
    const stub = stubClu({ 'download.act': () => null, 'download.fileAct': () => null })
    window.clu = stub.clu
    const a = dragEvent()
    startFileDrag(a, dragTargetOf(download({})))
    const b = dragEvent()
    startFileDrag(b, dragTargetOf(shot))
    const c = dragEvent()
    startFileDrag(c, dragTargetOf(download({ exists: false })))
    await Promise.resolve()
    expect([a, b, c].every((e) => e.preventDefault.mock.calls.length === 1)).toBe(true)
    expect(stub.calls).toEqual([
      { name: 'download.act', payload: { id: D, action: 'drag' } },
      { name: 'download.fileAct', payload: { source: 'media', name: 'a.png', action: 'drag' } }
    ])
  })
})
