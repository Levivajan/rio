// The Ask row's assistant glyphs: Rio's own 16 pt drawings of each service's mark, bundled so the overlay never
// loads an icon from the network (its CSP forbids it). Grok and ChatGPT draw in the row's ink, Claude in its orange.
import type { AssistantId } from '../../../shared/command/ask'

const box = { width: 16, height: 16, viewBox: '0 0 24 24', fill: 'none', strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const PETALS = [0, 60, 120, 180, 240, 300]
const RAYS = Array.from({ length: 12 }, (_, i) => i)

export function AssistantIcon({ id }: { id: AssistantId }) {
  switch (id) {
    case 'chatgpt':
      // Six thin links, each set off the center along a side of a hexagon and turned 60 degrees: the interlaced knot
      // in outline, not a six-petal flower.
      return (
        <svg {...box} stroke="currentColor" strokeWidth={1.4} data-glyph="chatgpt">
          {PETALS.map((deg) => (
            <rect key={deg} x="14" y="2.5" width="5" height="12" rx="2.5" transform={`rotate(${deg} 12 12)`} />
          ))}
        </svg>
      )
    case 'claude':
      // Twelve rays of two lengths from the center: the burst.
      return (
        <svg {...box} stroke="#d97757" strokeWidth={2.3} data-glyph="claude">
          {RAYS.map((i) => {
            const a = ((i * 30 + 8) * Math.PI) / 180
            const r = i % 2 === 0 ? 10 : 7.6
            return <path key={i} d={`M12 12L${(12 + r * Math.cos(a)).toFixed(2)} ${(12 + r * Math.sin(a)).toFixed(2)}`} />
          })}
        </svg>
      )
    case 'grok':
      // A ring left open at the top right, crossed by a slash.
      return (
        <svg {...box} stroke="currentColor" strokeWidth={2} data-glyph="grok">
          <path d="M16.6 5.6A8 8 0 1 0 19.6 13" />
          <path d="M4.5 19.5 20 4" />
        </svg>
      )
  }
}
