// Rio branding (code name clu) for the Chromium build (docs/chromium/PLAN.md 2.1, M1). Renders
// assets/brand/clu-icon.svg into the files Chromium's Mac build reads from chrome/app/theme/chromium and
// components/vector_icons/chromium. They are written to chromium/branding/clu/ under their upstream paths, and apply.sh
// copies that tree over the Chromium checkout.
// The app icon is rendered from the same master by scripts/make-icon.mjs. In-app logos crop away the macOS
// inset. actool from full Xcode (DEVELOPER_DIR) compiles Assets.car and app.icns from one catalog (iconutil on macOS
// 26.6 rejects every iconset, Chromium's own included).
import { createHash } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { Resvg } from '@resvg/resvg-js'
import { BRAND_SVG, ICON_ROOT, masterSvg, renderIcon } from '../make-icon.mjs'

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..')
export const OUT = join(ROOT, 'chromium', 'branding', 'clu')
/** The alternate app icons (docs/chromium/icons.md): assets/brand/app-icons/rio-<id>.svg, rendered for Clu's resources. */
export const ALTERNATES_SRC = join(ROOT, 'assets', 'brand', 'app-icons')
export const ALTERNATES_OUT = join(ROOT, 'chromium', 'clu', 'browser', 'app_icon', 'icons')
export const ALTERNATES = ['airySun', 'ocean', 'flatBlue', 'sunset', 'warm', 'dusk']
/** The Dock tile tops out at 256 pt, so 512 px covers a Retina Dock. */
export const ALTERNATE_SIZE = 512
const THEME = 'chrome/app/theme/chromium'
const XCODE = '/Applications/Xcode.app/Contents/Developer'

/** Upstream PNG paths (relative to the Chromium src root) and their pixel size. */
export const LOGOS = [
  ...[16, 24, 48, 64, 128, 256].map((s) => ({ path: `${THEME}/product_logo_${s}.png`, size: s })),
  { path: 'chrome/app/theme/default_100_percent/chromium/product_logo_16.png', size: 16 },
  { path: 'chrome/app/theme/default_100_percent/chromium/product_logo_32.png', size: 32 },
  { path: 'chrome/app/theme/default_200_percent/chromium/product_logo_16.png', size: 32 },
  { path: 'chrome/app/theme/default_200_percent/chromium/product_logo_32.png', size: 64 },
  // chrome://version's favicon (IDR_PRODUCT_FAVICON, components/resources).
  { path: 'components/resources/default_100_percent/chromium/favicon_product.png', size: 16 },
  { path: 'components/resources/default_200_percent/chromium/favicon_product.png', size: 32 }
]

/** The master's squircle on Apple's 1024 grid: 100 to 924. */
const SQUIRCLE = { x: 100, size: 824 }

/** The master rendered full bleed for the Icon Composer layer: its squircle fills the 1024 canvas, no shadow margin. */
export function fullBleed(mark) {
  const svg = masterSvg(mark).replace(ICON_ROOT,
    `<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="${SQUIRCLE.x} ${SQUIRCLE.x} ${SQUIRCLE.size} ${SQUIRCLE.size}">`)
  return new Resvg(svg, { fitTo: { mode: 'width', value: 1024 }, font: { loadSystemFonts: false } }).render().asPng()
}

/** AppIcon.icon/icon.json: one unglassed layer, the background the master's squircle has, macOS's own mask. */
export const ICON_COMPOSER = {
  fill: { solid: 'extended-gray:0.95000,1.00000' },
  groups: [{ layers: [{ 'image-name': 'rio.png', name: 'rio', glass: false }], specular: false, lighting: 'combined', shadow: { kind: 'none', opacity: 0 }, translucency: { enabled: false, value: 0 } }],
  'supported-platforms': { squares: 'shared' }
}

/** The "logo plus name" images (payment handler dialog), 22 px tall at 1x. */
export const WORDMARKS = [
  { path: 'chrome/app/theme/default_100_percent/chromium/product_logo_name_22.png', scale: 1, color: '#1F1F1F' },
  { path: 'chrome/app/theme/default_100_percent/chromium/product_logo_name_22_white.png', scale: 1, color: '#FFFFFF' },
  { path: 'chrome/app/theme/default_200_percent/chromium/product_logo_name_22.png', scale: 2, color: '#1F1F1F' },
  { path: 'chrome/app/theme/default_200_percent/chromium/product_logo_name_22_white.png', scale: 2, color: '#FFFFFF' }
]

/** chrome://version's logo (IDR_PRODUCT_LOGO and IDR_PRODUCT_LOGO_WHITE), 171 x 32 at 1x, set at its right edge. */
export const VERSION_LOGOS = [
  { path: 'components/resources/default_100_percent/chromium/product_logo.png', scale: 1, color: '#1F1F1F' },
  { path: 'components/resources/default_100_percent/chromium/product_logo_white.png', scale: 1, color: '#FFFFFF' },
  { path: 'components/resources/default_200_percent/chromium/product_logo.png', scale: 2, color: '#1F1F1F' },
  { path: 'components/resources/default_200_percent/chromium/product_logo_white.png', scale: 2, color: '#FFFFFF' }
]

/** The app icon without the macOS grid inset, for logos drawn inside the browser. */
export function logoSvg(mark) {
  return masterSvg(mark).replace('viewBox="0 0 1024 1024"', 'viewBox="100 100 824 824"')
}

function png(svg, width, fonts = false) {
  return new Resvg(svg, { fitTo: { mode: 'width', value: width }, font: { loadSystemFonts: fonts } }).render().asPng()
}

/** The menu bar status icon (IDR_STATUS_TRAY_ICON), 22 px: the orb in black with its wave lines cut out. */
export const MONO = { path: `${THEME}/product_logo_22_mono.png`, size: 22 }

/**
 * The wave fills of the brand icon, in paint order: the polygons under the orb, each either a gradient multiplied over
 * the orb (rio-airySun) or one flat color (rio-flatWarm). Each ends with the two bottom corners under its top edge.
 */
function waveFills(icon) {
  return [...icon.matchAll(/<path d="(M[\d.\s L]+Z)" fill="(?:url\(#[^)]+\)" style="mix-blend-mode:multiply|#[\dA-Fa-f]{6})"/g)].map(([, d]) =>
    [...d.matchAll(/[ML]\s*([\d.]+)[\s,]+([\d.]+)/g)].map((m) => [Number(m[1]), Number(m[2])])
  )
}

/** The orb: the circle the waves are clipped to. */
function orbCircle(icon) {
  const orb = /<clipPath id="[^"]*o(?:rb)?"><circle cx="([\d.]+)" cy="([\d.]+)" r="([\d.]+)"/.exec(icon)
  if (!orb) throw new Error('brand SVG has no orb')
  const [cx, cy, r] = orb.slice(1).map(Number)
  return { cx, cy, r }
}

/**
 * The orb (the clip circle of the waves) and the wave lines of the brand icon: its white strokes of straight segments
 * when it draws them, or else the top edges of its wave fills.
 */
export function orbParts(icon) {
  const { cx, cy, r } = orbCircle(icon)
  let waves = [...icon.matchAll(/<path d="(M[\d.\s L]+)" fill="none" stroke="#fff"/g)].map(([, d]) => d.trim())
  if (waves.length === 0) waves = waveFills(icon).map((poly) => `M${poly.slice(0, -2).map(([x, y]) => `${x} ${y}`).join(' L')}`)
  if (waves.length === 0) throw new Error('brand SVG has no wave lines')
  return { cx, cy, r, waves }
}

export function monoSvg(icon) {
  const { cx, cy, r, waves } = orbParts(icon)
  // Each cut is about 1.5 px wide at 22 px.
  const cut = waves.map((d) => `<path d="${d}" fill="none" stroke="#000000" stroke-width="${num(r * 0.14)}" stroke-linejoin="round"/>`).join('')
  return `<svg xmlns="http://www.w3.org/2000/svg" width="22" height="22" viewBox="${num(cx - r)} ${num(cy - r)} ${num(2 * r)} ${num(2 * r)}">
  <mask id="m"><rect x="${num(cx - r)}" y="${num(cy - r)}" width="${num(2 * r)}" height="${num(2 * r)}" fill="#FFFFFF"/>${cut}</mask>
  <circle cx="${num(cx)}" cy="${num(cy)}" r="${num(r)}" fill="#000000" mask="url(#m)"/>
</svg>`
}

export function wordmarkSvg(mark, color) {
  // Nested as an <svg> element: resvg drops an <image> whose SVG holds another <image>.
  const logo = masterSvg(mark).replace('width="1024" height="1024" viewBox="0 0 1024 1024"', 'x="0" y="0" width="22" height="22" viewBox="100 100 824 824"')
  return `<svg xmlns="http://www.w3.org/2000/svg" width="60" height="22" viewBox="0 0 60 22">
  ${logo}
  <text x="27" y="16.5" font-family="Helvetica Neue" font-weight="bold" font-size="16" fill="${color}">Rio</text>
</svg>`
}

export function versionLogoSvg(mark, color) {
  const logo = masterSvg(mark).replace('width="1024" height="1024" viewBox="0 0 1024 1024"', 'x="88" y="0" width="32" height="32" viewBox="100 100 824 824"')
  return `<svg xmlns="http://www.w3.org/2000/svg" width="171" height="32" viewBox="0 0 171 32">
  ${logo}
  <text x="171" y="25" text-anchor="end" font-family="Helvetica Neue" font-weight="bold" font-size="25" fill="${color}">Rio</text>
</svg>`
}

const num = (v) => String(Math.round(v * 100) / 100)
const argb = (hex) => `0xFF, ${[1, 3, 5].map((i) => `0x${hex.slice(i, i + 2).toUpperCase()}`).join(', ')}`

/** Sutherland-Hodgman: `poly` clipped to the circle (cx, cy, r) taken as a regular `n`-gon. */
export function clipToCircle(poly, cx, cy, r, n = 72) {
  let out = poly
  for (let k = 0; k < n && out.length > 0; k++) {
    const a = [cx + r * Math.cos((2 * Math.PI * k) / n), cy + r * Math.sin((2 * Math.PI * k) / n)]
    const b = [cx + r * Math.cos((2 * Math.PI * (k + 1)) / n), cy + r * Math.sin((2 * Math.PI * (k + 1)) / n)]
    const side = (p) => (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
    const sign = Math.sign(side([cx, cy]))
    const inside = (p) => side(p) * sign >= 0
    const cut = (p, q) => {
      const t = side(p) / (side(p) - side(q))
      return [p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])]
    }
    const next = []
    out.forEach((p, i) => {
      const q = out[(i + 1) % out.length]
      if (inside(q)) {
        if (!inside(p)) next.push(cut(p, q))
        next.push(q)
      } else if (inside(p)) next.push(cut(p, q))
    })
    out = next
  }
  return out
}

/**
 * The brand orb as a Chromium vector icon (ui/gfx/vector_icon_types.h) on a 24 dp canvas, the orb filling 22 dp: a
 * half-dp rim, the core, and each wave band clipped to the core. The artwork's gradients, blend modes and gloss have
 * no vector icon form, so each part takes the flat color the rendered icon shows in its middle. The waves are the
 * polygons filled under the wave lines, in paint order; anything else throws, so a changed brand file can't produce a
 * wrong icon.
 */
export function vectorIcon(icon) {
  const { cx, cy, r } = orbCircle(icon)
  const fills = waveFills(icon)
  if (fills.length === 0) throw new Error('brand SVG has no wave fills')
  const image = new Resvg(masterSvg(icon), { fitTo: { mode: 'width', value: 1024 }, font: { loadSystemFonts: false } }).render()
  const color = (x, y) => {
    const i = (Math.round(y) * image.width + Math.round(x)) * 4
    return `#${[0, 1, 2].map((k) => image.pixels[i + k].toString(16).padStart(2, '0')).join('')}`
  }
  // Where a wave's top edge crosses the orb's vertical center line.
  const top = (poly) => poly.slice(0, -2).reduce((best, p) => (Math.abs(p[0] - cx) < Math.abs(best[0] - cx) ? p : best))[1]
  const tops = fills.map(top)
  const k = 11 / r // source units to dp
  const core = r * (10.5 / 11)
  const dp = ([x, y]) => [num(12 + (x - cx) * k), num(12 + (y - cy) * k)]
  const out = ['// Generated from assets/brand/clu-icon.svg by scripts/chromium/make-branding.mjs. Do not edit.', '', 'CANVAS_DIMENSIONS, 24,']
  out.push(`PATH_COLOR_ARGB, ${argb(color(cx, cy - r + 1))},`, 'CIRCLE, 12, 12, 11,')
  out.push('NEW_PATH,', `PATH_COLOR_ARGB, ${argb(color(cx, cy - r / 2))},`, 'CIRCLE, 12, 12, 10.5,')
  fills.forEach((poly, i) => {
    // The band shows from its top edge down to the next wave painted over it, or the bottom of the orb.
    const below = Math.min(cy + core, ...tops.slice(i + 1).filter((t) => t > tops[i]))
    const points = clipToCircle(poly, cx, cy, core).map(dp).filter((p, j, all) => j === 0 || p.join() !== all[j - 1].join())
    if (points.length < 3) return
    out.push('NEW_PATH,', `PATH_COLOR_ARGB, ${argb(color(cx, (tops[i] + below) / 2))},`)
    points.forEach((p, j) => out.push(`${j === 0 ? 'MOVE_TO' : 'LINE_TO'}, ${p.join(', ')},`))
    out.push('CLOSE,')
  })
  return `${out.join('\n')}\n`
}

function write(out, rel, data) {
  mkdirSync(dirname(join(out, rel)), { recursive: true })
  writeFileSync(join(out, rel), data)
}

export function makeBranding(out = OUT, mark = readFileSync(BRAND_SVG, 'utf8')) {
  new Resvg(mark) // a broken brand file throws here instead of rendering an empty square
  rmSync(out, { recursive: true, force: true })
  for (const { path, size } of LOGOS) write(out, path, png(logoSvg(mark), size))
  for (const { path, scale, color } of WORDMARKS) write(out, path, png(wordmarkSvg(mark, color), 60 * scale, true))
  for (const { path, scale, color } of VERSION_LOGOS) write(out, path, png(versionLogoSvg(mark, color), 171 * scale, true))
  write(out, MONO.path, png(monoSvg(mark), MONO.size))
  write(out, `${THEME}/product_logo.svg`, logoSvg(mark))
  write(out, `${THEME}/product_logo_animation.svg`, logoSvg(mark))
  // Chromium's settings toolbar shows this in dark mode instead of product_logo_32.png (cr_toolbar).
  write(out, 'ui/webui/resources/images/chrome_logo_dark.svg', logoSvg(mark))
  const icon = vectorIcon(mark)
  write(out, 'components/vector_icons/chromium/product.icon', icon)
  write(out, 'components/vector_icons/chromium/product_refresh.icon', icon)

  const tmp = mkdtempSync(join(tmpdir(), 'clu-branding-'))
  try {
    // Same catalog shape as upstream: an Icon Composer AppIcon.icon beside the catalog with its "Icon" image set.
    // macOS 26 puts a pre-masked icon (an appiconset) inside a gray frame, so the .icon carries the master's art
    // full bleed (the 824 pt squircle scaled to the 1024 canvas) and macOS draws its own mask. actool also writes the
    // AppIcon.icns older systems show.
    const xc = join(tmp, 'Assets.xcassets')
    mkdirSync(xc, { recursive: true })
    writeFileSync(join(xc, 'Contents.json'), JSON.stringify({ info: { author: 'xcode', version: 1 } }))
    const appIcon = join(tmp, 'AppIcon.icon')
    mkdirSync(join(appIcon, 'Assets'), { recursive: true })
    writeFileSync(join(appIcon, 'Assets', 'rio.png'), fullBleed(mark))
    writeFileSync(join(appIcon, 'icon.json'), JSON.stringify(ICON_COMPOSER, null, 2))
    const icons = join(xc, 'Icon.iconset')
    mkdirSync(icons)
    writeFileSync(join(icons, 'icon_256x256.png'), renderIcon(mark, 256))
    writeFileSync(join(icons, 'icon_256x256@2x.png'), renderIcon(mark, 512))
    const car = join(tmp, 'car')
    mkdirSync(car)
    execFileSync('xcrun', ['actool', '--compile', car, '--platform', 'macosx', '--minimum-deployment-target', '12.0',
      '--app-icon', 'AppIcon', '--output-partial-info-plist', join(tmp, 'partial.plist'), '--errors', xc, appIcon],
    { env: { ...process.env, DEVELOPER_DIR: process.env.DEVELOPER_DIR ?? XCODE }, stdio: ['ignore', 'ignore', 'inherit'] })
    mkdirSync(join(out, THEME, 'mac'), { recursive: true })
    writeFileSync(join(out, THEME, 'mac', 'Assets.car'), readFileSync(join(car, 'Assets.car')))
    writeFileSync(join(out, THEME, 'mac', 'app.icns'), readFileSync(join(car, 'AppIcon.icns')))
  } finally {
    rmSync(tmp, { recursive: true, force: true })
  }
  // check-brand.sh compares this hash with the brand SVG, so a changed SVG without a regenerated tree fails.
  writeFileSync(join(dirname(out), 'clu-icon.sha256'), `${createHash('sha256').update(mark).digest('hex')}  assets/brand/clu-icon.svg\n`)
}

/** Each alternate's PNG, named by its id, for clu_app_icon_resources.grd and the Dock tile plug-in. */
export function makeAlternates(out = ALTERNATES_OUT, src = ALTERNATES_SRC) {
  mkdirSync(out, { recursive: true })
  for (const id of ALTERNATES) writeFileSync(join(out, `${id}.png`), renderIcon(readFileSync(join(src, `rio-${id}.svg`), 'utf8'), ALTERNATE_SIZE))
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  makeBranding()
  makeAlternates()
  process.stdout.write('wrote chromium/branding/clu/, chromium/branding/clu-icon.sha256 and chromium/clu/browser/app_icon/icons/\n')
}
