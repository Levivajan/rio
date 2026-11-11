// The Library's Downloads and Media lists: merging Rio's downloads with the Downloads folder, Media
// filtering, search, day groups and junk rows from the browser.
import { describe, expect, it } from 'vitest'
import type { LibraryFile } from '../../../shared/ipc/capabilities'
import type { DownloadState } from '../../../shared/state/capabilities'
import { cleanFiles, downloadItems, fileKindLine, groupItems, itemSubtitle, kindOf, mediaItems, sourceSite } from './libraryModel'

const NOW = new Date(2026, 10, 9, 19, 0).getTime()
const file = (name: string, minutesAgo: number, kind: LibraryFile['kind'] = 'file'): LibraryFile => ({ name, size: 10, modifiedAt: NOW - minutesAgo * 60_000, kind })
const download = (filename: string, minutesAgo: number): DownloadState => ({
  id: `id-${filename}`, profileId: 'p', filename, url: `https://a.test/${filename}`, state: 'completed', receivedBytes: 10, totalBytes: 10,
  bytesPerSecond: 0, etaMs: null, startedAt: NOW - minutesAgo * 60_000 - 1000, endedAt: NOW - minutesAgo * 60_000, exists: true
})

describe('downloadItems', () => {
  it('lists Rio downloads and folder files newest first, a file Rio downloaded once', () => {
    const items = downloadItems([download('invoice.pdf', 5)], [file('invoice.pdf', 5), file('older.zip', 60), file('newer.dmg', 1)])
    expect(items.map((i) => [i.type, i.name])).toEqual([
      ['file', 'newer.dmg'],
      ['download', 'invoice.pdf'],
      ['file', 'older.zip']
    ])
  })

  it('filters by a case-insensitive search', () => {
    const items = downloadItems([download('Invoice.pdf', 5)], [file('older.zip', 60)], '  INVOICE ')
    expect(items.map((i) => i.name)).toEqual(['Invoice.pdf'])
    expect(downloadItems([], [file('a.zip', 1)], 'nothing')).toEqual([])
  })
})

describe('mediaItems', () => {
  it('keeps images and videos only, newest first, searchable', () => {
    const files = [file('notes.txt', 1), file('Screenshot 1.png', 10, 'image'), file('Screen Recording.mov', 3, 'video')]
    expect(mediaItems(files).map((i) => i.name)).toEqual(['Screen Recording.mov', 'Screenshot 1.png'])
    expect(mediaItems(files, 'record').map((i) => i.name)).toEqual(['Screen Recording.mov'])
    expect(mediaItems(files)[0]).toMatchObject({ type: 'file', source: 'media' })
  })
})

describe('groupItems', () => {
  it('groups by relative day: Today, 1 day ago, 4 days ago', () => {
    const items = mediaItems([file('a.png', 1, 'image'), file('b.png', 60, 'image'), file('c.png', 24 * 60, 'image'), file('d.png', 4 * 24 * 60, 'image')])
    const groups = groupItems(items, NOW)
    expect(groups.map((g) => [g.label, g.items.length])).toEqual([
      ['Today', 2],
      ['1 day ago', 1],
      ['4 days ago', 1]
    ])
  })
})

describe('subtitles', () => {
  it('names the kind and the site a download came from', () => {
    expect(fileKindLine('Receipt.PDF', 'file', 'https://doc-0s-4k.googleusercontent.com/x')).toBe('PDF from googleusercontent.com')
    expect(fileKindLine('shot.png', 'image', 'https://www.example.co.uk/a.png')).toBe('Image from example.co.uk')
    expect(fileKindLine('cut.mov', 'video')).toBe('Video')
    expect(fileKindLine('data.csv', 'file', 'not a url')).toBe('Download')
    expect(sourceSite('http://127.0.0.1:3740/x')).toBe('127.0.0.1')
    expect(sourceSite('blob:junk')).toBe('')
  })

  it('shows the status of a download that is running or gone', () => {
    const [done] = downloadItems([download('invoice.pdf', 5)], [])
    expect(done && itemSubtitle(done)).toBe('PDF from a.test')
    const running = { ...download('big.zip', 1), state: 'progressing' as const, receivedBytes: 5 }
    const [row] = downloadItems([running], [])
    expect(row && itemSubtitle(row)).toContain('of')
    const [folder] = downloadItems([], [file('notes.txt', 5)])
    expect(folder && itemSubtitle(folder)).toBe('Download')
  })
})

describe('cleanFiles', () => {
  it('drops junk rows and fills a missing kind from the name', () => {
    const rows = cleanFiles([
      null,
      { name: '' },
      { name: '../etc/passwd', size: 1, modifiedAt: 1, kind: 'file' },
      { name: 'shot.PNG', size: Number.NaN, modifiedAt: 'soon' },
      { name: 'x'.repeat(400), size: -5, modifiedAt: 2, kind: 'weird' },
      42
    ])
    expect(rows).toEqual([
      { name: 'shot.PNG', size: 0, modifiedAt: 0, kind: 'image' },
      { name: 'x'.repeat(255), size: 0, modifiedAt: 2, kind: 'file' }
    ])
    expect(cleanFiles({ rows: [] })).toEqual([])
  })

  it('knows image and video names', () => {
    expect([kindOf('a.heic'), kindOf('b.MOV'), kindOf('c.pdf')]).toEqual(['image', 'video', 'file'])
  })
})
