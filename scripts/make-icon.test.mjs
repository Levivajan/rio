import { execFileSync } from 'node:child_process'
import { mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { describe, expect, it } from 'vitest'
import { BRAND_SVG, ICON_ROOT, ICONSET, makeIcon, masterSvg, pngSize, renderIcon } from './make-icon.mjs'

const mark = readFileSync(BRAND_SVG, 'utf8')

describe('make-icon', () => {
  it('takes the brand SVG as the master: a whole 1024 icon on the macOS grid, nothing added', () => {
    expect(masterSvg(mark)).toBe(mark)
    expect(mark.startsWith(ICON_ROOT)).toBe(true)
    // Nothing is fetched or embedded: every reference is to the file's own defs.
    expect([...mark.matchAll(/(?:href|url\()\s*=?\s*"?([^")\s]*)/g)].every(([, ref]) => ref.startsWith('#'))).toBe(true)
    expect(() => masterSvg('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48"><circle r="1"/></svg>')).toThrow(/1024/)
  })

  it('lists the 10 iconset representations iconutil expects', () => {
    expect(ICONSET.map((i) => i.name)).toEqual([
      'icon_16x16.png', 'icon_16x16@2x.png', 'icon_32x32.png', 'icon_32x32@2x.png', 'icon_128x128.png',
      'icon_128x128@2x.png', 'icon_256x256.png', 'icon_256x256@2x.png', 'icon_512x512.png', 'icon_512x512@2x.png'
    ])
    expect(ICONSET.map((i) => i.size)).toEqual([16, 32, 32, 64, 128, 256, 256, 512, 512, 1024])
  })

  it('renders exact sizes', () => {
    expect(pngSize(renderIcon(mark, 1024))).toEqual({ width: 1024, height: 1024 })
    expect(pngSize(renderIcon(mark, 16))).toEqual({ width: 16, height: 16 })
  })

  it('reads no size from bytes that are not a PNG', () => {
    expect(pngSize(Buffer.from('not a png at all, just text bytes'))).toBeNull()
    expect(pngSize(Buffer.alloc(0))).toBeNull()
    expect(pngSize(null)).toBeNull()
  })

  it.skipIf(process.platform !== 'darwin')('writes icon.png and an icns that round-trips to 10 representations', { timeout: 30_000 }, () => {
    const dir = mkdtempSync(join(tmpdir(), 'clu-icon-'))
    try {
      makeIcon(dir, mark)
      expect(pngSize(readFileSync(join(dir, 'icon.png')))).toEqual({ width: 1024, height: 1024 })
      const back = join(dir, 'back.iconset')
      execFileSync('iconutil', ['-c', 'iconset', join(dir, 'icon.icns'), '-o', back])
      expect(readdirSync(back).filter((f) => f.endsWith('.png'))).toHaveLength(10)
    } finally {
      rmSync(dir, { recursive: true, force: true })
    }
  })

  it.skipIf(process.platform !== 'darwin')('throws on a broken brand SVG instead of keeping a stale icon', { timeout: 30_000 }, () => {
    const dir = mkdtempSync(join(tmpdir(), 'clu-icon-bad-'))
    try {
      expect(() => makeIcon(dir, '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1"><bad')).toThrow()
    } finally {
      rmSync(dir, { recursive: true, force: true })
    }
  })
})
