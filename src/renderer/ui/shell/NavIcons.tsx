// Top-control icons of the shell module: Clu's own line icons in the scaffold's 16 px style (stop, copy link, its check). The
// top row's reload and the pill's copy link are drawn at their own sizes.
import type { SVGProps } from 'react'

const base = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const

/** The loading stop: an × drawn 12.5 x 12.5. */
export const StopIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="m2.55 2.55 10.9 10.9m0-10.9-10.9 10.9" /></svg>
)
/** The top-row reload: 14.2 x 16.7, an open circle with the arrowhead standing above its top. */
export const ToolbarReloadIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} width={18} height={18} viewBox="0 0 18 18" {...p}>
    <path d="M9 4.55a6.05 6.05 0 1 0 5.24 3.03" />
    <path d="m8.2 1.55 3 3-3 3" />
  </svg>
)
/** The pill's link icon at 14.7 pt: the
 *  scaffold's link scaled 1.29 about the center of its 16 box, stroke kept at 1.6. */
export const CopyLinkIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}>
    <g transform="matrix(1.29 0 0 1.29 -2.32 -2.32)" strokeWidth={1.6 / 1.29}>
      <path d="M7 9a2.5 2.5 0 0 0 3.5 0l2-2a2.5 2.5 0 0 0-3.5-3.5l-.5.5" />
      <path d="M9 7a2.5 2.5 0 0 0-3.5 0l-2 2A2.5 2.5 0 0 0 7 12.5l.5-.5" />
    </g>
  </svg>
)
/** The copy button's ✓ for a second after a copy. */
export const CheckIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="m3.5 8.5 3 3 6-7" /></svg>
)
/** The narrow top row's "...": three 2.5 pt dots on a 5 pt pitch. */
export const MoreIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={16} height={16} viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" {...p}>
    <circle cx="3" cy="8" r="1.4" />
    <circle cx="8" cy="8" r="1.4" />
    <circle cx="13" cy="8" r="1.4" />
  </svg>
)
