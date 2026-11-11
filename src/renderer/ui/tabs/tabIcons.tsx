// Line icons of the tabs module: speaker, muted speaker, minus. 16 px, stroke uses currentColor, like common/icons.
import type { SVGProps } from 'react'

const base = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const

export const SpeakerIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}>
    <path d="M2.5 6.2h2.2L8 3.5v9L4.7 9.8H2.5z" />
    <path d="M10.5 5.8a3 3 0 0 1 0 4.4M12.3 4a5.5 5.5 0 0 1 0 8" />
  </svg>
)
export const SpeakerMutedIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}>
    <path d="M2.5 6.2h2.2L8 3.5v9L4.7 9.8H2.5z" />
    <path d="m10.5 6 3.5 4m0-4-3.5 4" />
  </svg>
)
export const MinusIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}>
    <path d="M4 8h8" />
  </svg>
)
