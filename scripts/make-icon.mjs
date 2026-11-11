// App icon from assets/brand/clu-icon.svg (docs/plan/09-platform.md F12, docs/chromium/icons.md).
// The brand SVG is the whole 1024 icon on Apple's macOS grid (an 824 pt body inset 100 with its own tile, shading and
// shadow), so it is the master as it is; this renders the iconset sizes and build/icon.icns via iconutil. resvg draws
// everything in it (blend modes, grain, blur) to within 1/255 of the artwork's PNG. System fonts are off.
import { execFileSync } from 'node:child_process'
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { Resvg } from '@resvg/resvg-js'

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..')
export const BRAND_SVG = join(ROOT, 'assets', 'brand', 'clu-icon.svg')

/** The iconset file names iconutil expects: 16, 32, 128, 256 and 512 at 1x and 2x (10 representations). */
export const ICONSET = [16, 32, 128, 256, 512].flatMap((s) => [
  { name: `icon_${s}x${s}.png`, size: s },
  { name: `icon_${s}x${s}@2x.png`, size: s * 2 }
])

/** The root element every icon SVG starts with: the 1024 canvas of Apple's macOS icon grid. */
export const ICON_ROOT = '<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">'

/** The 1024 master: the icon SVG itself. Anything that is not a 1024 grid icon throws, so a stray mark can't ship. */
export function masterSvg(icon) {
  if (!icon.trimStart().startsWith(ICON_ROOT)) throw new Error('icon SVG is not a 1024 macOS grid icon')
  return icon
}

export function renderIcon(icon, size) {
  return new Resvg(masterSvg(icon), { fitTo: { mode: 'width', value: size }, font: { loadSystemFonts: false } }).render().asPng()
}

/** Width and height from a PNG's IHDR chunk, or null when the bytes are not a PNG. */
export function pngSize(buf) {
  const sig = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]
  if (!buf || buf.length < 24 || sig.some((b, i) => buf[i] !== b) || buf.toString('latin1', 12, 16) !== 'IHDR') return null
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) }
}

export function makeIcon(out = join(ROOT, 'build'), icon = readFileSync(BRAND_SVG, 'utf8')) {
  // A broken brand file throws here instead of leaving a stale icon.
  new Resvg(masterSvg(icon))
  mkdirSync(out, { recursive: true })
  writeFileSync(join(out, 'icon.png'), renderIcon(icon, 1024))
  const iconset = join(out, 'icon.iconset')
  rmSync(iconset, { recursive: true, force: true })
  mkdirSync(iconset)
  for (const { name, size } of ICONSET) writeFileSync(join(iconset, name), renderIcon(icon, size))
  rmSync(join(out, 'icon.icns'), { force: true })
  execFileSync('iconutil', ['-c', 'icns', iconset, '-o', join(out, 'icon.icns')])
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  makeIcon()
  process.stdout.write('wrote build/icon.png, build/icon.iconset/ and build/icon.icns\n')
}
