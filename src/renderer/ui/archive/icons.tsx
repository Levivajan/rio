// Archive panel icons: 16 px line icons in the scaffold's style, stroke uses currentColor.
import type { SVGProps } from 'react'

const base = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const

export const ArchiveIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><rect x="2.5" y="3" width="11" height="3" rx="1" /><path d="M3.5 6v6a1 1 0 0 0 1 1h7a1 1 0 0 0 1-1V6M6.5 8.5h3" /></svg>
)
/** The footer's library button: a filled box in the toolbar glyph's color. It rests closed; the
 * lid tips open and two sparkles come out only on hover and when the Library opens (archive.css). The slot
 * is cut out of the body, so the sidebar shows through it. */
export const LibraryIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} width={18} height={18} viewBox="0 0 18 18" stroke="none" fill="currentColor" {...p}>
    <g className="clu-library-sparkles">
      <path d="M12.9.9Q13.25 2.45 14.8 2.8 13.25 3.15 12.9 4.7 12.55 3.15 11 2.8 12.45 2.45 12.9.9Z" />
      <path d="M15.6 4.9Q15.78 5.72 16.6 5.9 15.78 6.08 15.6 6.9 15.42 6.08 14.6 5.9 15.42 5.72 15.6 4.9Z" />
    </g>
    <rect className="clu-library-lid" x="3.15" y="5.9" width="11.7" height="3.1" rx="1.1" />
    <path fillRule="evenodd" d="M4.95 9.6h8.1a1.2 1.2 0 0 1 1.2 1.2v4.5a1.4 1.4 0 0 1-1.4 1.4h-7.7a1.4 1.4 0 0 1-1.4-1.4v-4.5a1.2 1.2 0 0 1 1.2-1.2ZM7.4 11.7a.6.6 0 0 0 0 1.2h3.2a.6.6 0 0 0 0-1.2Z" />
  </svg>
)
