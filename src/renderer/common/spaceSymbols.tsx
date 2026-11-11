// The space icon glyphs: 40 monochrome line symbols drawn for Rio on a 16 px grid, stroke in currentColor,
// in the style of icons.tsx. Rio owns them; no third-party icon set is copied. SPACE_SYMBOLS in src/shared/types.ts
// names them and the C++ list in clu_space_rules.cc matches it.
import type { SVGProps } from 'react'
import { SPACE_SYMBOLS, type SpaceIcon, type SpaceSymbol } from '../../shared/types'

const circle = (cx: number, cy: number, r: number) => `M${cx - r} ${cy}a${r} ${r} 0 1 0 ${2 * r} 0a${r} ${r} 0 1 0 ${-2 * r} 0Z`
const rect = (x: number, y: number, w: number, h: number, r: number) =>
  `M${x + r} ${y}h${w - 2 * r}a${r} ${r} 0 0 1 ${r} ${r}v${h - 2 * r}a${r} ${r} 0 0 1 ${-r} ${r}h${-(w - 2 * r)}a${r} ${r} 0 0 1 ${-r} ${-r}v${-(h - 2 * r)}a${r} ${r} 0 0 1 ${r} ${-r}Z`

/** Each glyph's path data, one string per stroke. */
export const SPACE_SYMBOL_PATHS: Record<SpaceSymbol, readonly string[]> = {
  planet: [circle(8, 8, 3.8), 'M3.1 9.9C1.8 11.4 1.4 12.7 2 13.4c1 1.1 4.7-.6 8.1-4s5.2-7.1 4-8.1c-.6-.6-1.9-.2-3.4 1.1'],
  globe: [circle(8, 8, 6), 'M8 2c-3.3 3.6-3.3 8.4 0 12M8 2c3.3 3.6 3.3 8.4 0 12M2 8h12'],
  house: ['M2.5 7.5 8 3l5.5 4.5', 'M4 6.5V13h8V6.5', 'M6.8 13v-3h2.4v3'],
  briefcase: [rect(2, 5, 12, 8, 1.5), 'M5.5 5V3.8c0-.4.4-.8.8-.8h3.4c.4 0 .8.4.8.8V5', 'M2 9h12'],
  laptop: [rect(3, 3.5, 10, 7, 1), 'M1.5 12.5h13'],
  code: ['M5.5 4.5 2 8l3.5 3.5', 'M10.5 4.5 14 8l-3.5 3.5', 'M9 3 7 13'],
  terminal: [rect(2, 3, 12, 10, 1.5), 'M4.5 6.5 6.5 8l-2 1.5', 'M8 10h3.5'],
  book: ['M8 4.5C6.5 3.4 4.5 3 2.5 3v9.5c2 0 4 .4 5.5 1.5 1.5-1.1 3.5-1.5 5.5-1.5V3c-2 0-4 .4-5.5 1.5Z', 'M8 4.5V14'],
  bookmark: ['M4.5 2.5h7v11L8 10.8l-3.5 2.7Z'],
  folder: ['M2 4.5c0-.6.4-1 1-1h3l1.5 1.5H13c.6 0 1 .4 1 1V12c0 .6-.4 1-1 1H3c-.6 0-1-.4-1-1Z'],
  box: ['M8 2l5.5 2.8v6.4L8 14l-5.5-2.8V4.8Z', 'M2.5 4.8 8 7.6l5.5-2.8', 'M8 7.6V14'],
  pencil: ['M10.5 2.5l3 3-8 8h-3v-3Z', 'M9 4l3 3'],
  palette: [
    'M8 2a6 6 0 1 0 0 12c.8 0 1.2-.6 1.2-1.2 0-.9-.7-1.2-.7-2 0-.7.6-1.3 1.3-1.3h1.7A2.5 2.5 0 0 0 14 7c0-2.8-2.7-5-6-5Z',
    circle(5, 7.5, 0.5),
    circle(7, 4.8, 0.5),
    circle(10.2, 5, 0.5)
  ],
  camera: ['M2 5.5c0-.6.4-1 1-1h2l1-1.5h4l1 1.5h2c.6 0 1 .4 1 1V12c0 .6-.4 1-1 1H3c-.6 0-1-.4-1-1Z', circle(8, 8.5, 2.3)],
  film: [rect(2.5, 2.5, 11, 11, 1.5), 'M5.5 2.5v11M10.5 2.5v11', 'M2.5 6h3M2.5 10h3M10.5 6h3M10.5 10h3'],
  music: ['M6 12V3.5L13 2v8.5', circle(4.5, 12, 1.5), circle(11.5, 10.5, 1.5)],
  gamepad: [
    'M5 5h6a3 3 0 0 1 2.9 3.7l-.6 2.6a1.6 1.6 0 0 1-2.7.7l-1.3-1.5H6.7L5.4 12a1.6 1.6 0 0 1-2.7-.7l-.6-2.6A3 3 0 0 1 5 5Z',
    'M5 7v2M4 8h2',
    circle(10.8, 8, 0.5)
  ],
  cart: ['M1.5 2.5h2l1.6 7.5h7l1.4-5.5H4.2', circle(6, 12.8, 1), circle(11.5, 12.8, 1)],
  dollar: ['M8 1.5v13', 'M11 4.5C10.5 3.6 9.4 3 8 3 6.3 3 5 3.9 5 5.2c0 3 6 1.6 6 4.8 0 1.3-1.3 2.5-3 2.5-1.5 0-2.7-.6-3.2-1.7'],
  chart: ['M2.5 2.5v11h11', 'M5.5 10.5v-3M8.5 10.5v-6M11.5 10.5V8'],
  calendar: [rect(2, 3.5, 12, 10, 1.5), 'M2 6.5h12', 'M5 2v3M11 2v3'],
  chat: ['M2.5 4c0-.8.7-1.5 1.5-1.5h8c.8 0 1.5.7 1.5 1.5v5.5c0 .8-.7 1.5-1.5 1.5H7l-3 2.5V11c-.8 0-1.5-.7-1.5-1.5Z'],
  bell: ['M4 11V7a4 4 0 0 1 8 0v4l1.2 1.5H2.8Z', 'M6.5 14h3'],
  megaphone: ['M2.5 6.5v3h2l6 3.5v-10l-6 3.5Z', 'M4.5 9.5l1 4H7', 'M13 6.5v3'],
  bulb: ['M6 11.5h4M6.5 13.8h3', 'M6 11.5c0-1.6-2.5-2.6-2.5-5.5a4.5 4.5 0 0 1 9 0c0 2.9-2.5 3.9-2.5 5.5'],
  flask: ['M6 2h4', 'M6.8 2v4.2L3 12.6c-.4.7.1 1.4.9 1.4h8.2c.8 0 1.3-.7.9-1.4L9.2 6.2V2', 'M4.6 10h6.8'],
  gear: [circle(8, 8, 3.6), circle(8, 8, 1.4), 'M8 1.5v2.9M8 11.6v2.9M1.5 8h2.9M11.6 8h2.9M3.4 3.4l2 2M10.6 10.6l2 2M3.4 12.6l2-2M10.6 5.4l2-2'],
  wrench: ['M10.5 2a3.5 3.5 0 0 0-3.3 4.6l-4.7 4.7a1.6 1.6 0 0 0 2.2 2.2l4.7-4.7A3.5 3.5 0 0 0 14 5.5l-2 2-2-.5-.5-2 2-2a3.5 3.5 0 0 0-1-.5Z'],
  rocket: ['M13.5 2.5c-3.5 0-6 2-8 5l3 3c3-2 5-4.5 5-8Z', 'M5.5 7.5H3l-1 2 3 .5', 'M8.5 10.5V13l-2 1-.5-3', circle(10.3, 5.7, 1)],
  plane: ['M14 2 2 7l5 2 2 5Z', 'M7 9l7-7'],
  star: ['M8 1.8l1.9 3.9 4.3.6-3.1 3 .7 4.3L8 11.6l-3.8 2 .7-4.3-3.1-3 4.3-.6Z'],
  heart: ['M8 13.5S2 10 2 6a3 3 0 0 1 6-1 3 3 0 0 1 6 1c0 4-6 7.5-6 7.5Z'],
  flame: ['M8 14c-2.8 0-4.5-1.9-4.5-4.4 0-3 2.5-4.2 3-7.6 2 1.2 3 3 3 4.6.6-.4 1-1.1 1.2-1.9 1.3 1.1 1.8 2.8 1.8 4.9 0 2.5-1.7 4.4-4.5 4.4Z'],
  bolt: ['M9 1.5 3.5 9h4L7 14.5 12.5 7h-4Z'],
  sun: [circle(8, 8, 2.8), 'M8 1.5V3M8 13v1.5M1.5 8H3M13 8h1.5M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1'],
  moon: ['M13 9.5A5.5 5.5 0 1 1 6.5 3 4.5 4.5 0 0 0 13 9.5Z'],
  cloud: ['M4.5 12.5a3 3 0 0 1-.4-6A4 4 0 0 1 11.8 6a3.3 3.3 0 0 1-.3 6.5Z'],
  leaf: ['M3 13c0-6 3.5-10 10.5-10.5C13 9.5 9 13 3 13Z', 'M3 13l6-6'],
  paw: [
    'M8 8.5c-1.8 0-3.5 2-3.5 3.5 0 1 .8 1.5 1.7 1.5.8 0 1.2-.4 1.8-.4s1 .4 1.8.4c.9 0 1.7-.5 1.7-1.5 0-1.5-1.7-3.5-3.5-3.5Z',
    circle(3.5, 7.5, 1.1),
    circle(6, 4.5, 1.1),
    circle(10, 4.5, 1.1),
    circle(12.5, 7.5, 1.1)
  ],
  cup: ['M3 5.5h8v4a3.5 3.5 0 0 1-3.5 3.5h-1A3.5 3.5 0 0 1 3 9.5Z', 'M11 6.5h1a1.8 1.8 0 0 1 0 3.6h-1.2', 'M5.5 2v1.5M8.5 2v1.5']
}

/** The name the picker shows and searches: "Planet", "Gamepad". */
export const symbolLabel = (symbol: SpaceSymbol) => symbol.charAt(0).toUpperCase() + symbol.slice(1)

/** The symbols whose name holds the query ("pl" finds Planet and Plane); every symbol for an empty one. */
export function searchSymbols(query: string): SpaceSymbol[] {
  const q = query.trim().toLowerCase()
  return q ? SPACE_SYMBOLS.filter((s) => s.includes(q)) : [...SPACE_SYMBOLS]
}

export function SpaceSymbolGlyph({ symbol, size = 16, ...rest }: { symbol: SpaceSymbol; size?: number } & SVGProps<SVGSVGElement>) {
  const paths = SPACE_SYMBOL_PATHS[symbol]
  if (!paths) return null
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 16 16"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.4}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      data-symbol={symbol}
      {...rest}
    >
      {paths.map((d, i) => (
        <path key={i} d={d} />
      ))}
    </svg>
  )
}

/** True when a space icon draws a glyph (an emoji or a symbol Rio bundles), false for the letter icon. */
export const hasGlyph = (icon: SpaceIcon | undefined): boolean =>
  (icon?.kind === 'emoji' && !!icon.value) || (icon?.kind === 'symbol' && !!SPACE_SYMBOL_PATHS[icon.value])

/** A space icon as it draws in the sidebar, the bottom bar and Library: the emoji, the symbol glyph, or null for the
 * letter (each caller has its own letter or dot fallback). */
export function SpaceIconGlyph({ icon, size = 16, className }: { icon: SpaceIcon | undefined; size?: number; className?: string }) {
  if (!icon || !hasGlyph(icon)) return null
  if (icon.kind === 'emoji') {
    return (
      <span className={className} data-icon="emoji" aria-hidden="true">
        {icon.value}
      </span>
    )
  }
  if (icon.kind === 'symbol') {
    return (
      <span className={className} data-icon="symbol" aria-hidden="true">
        <SpaceSymbolGlyph symbol={icon.value} size={size} />
      </span>
    )
  }
  return null
}
