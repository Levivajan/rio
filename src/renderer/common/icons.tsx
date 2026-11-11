// Clu's own line icons. 16 px, stroke uses currentColor.
import type { SVGProps } from 'react'

const base = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const

/** Back and forward are mirrored arrows with a shaft, both drawn 14.7 x 11.6 at stroke 1.6, which reads 15 x 12 at 2x. */
export const BackIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="M14.55 8H1.45M6.45 3 1.45 8l5 5" /></svg>
)
export const ForwardIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="M1.45 8h13.1M9.55 3 14.55 8l-5 5" /></svg>
)
/** The PlusIcon of the footer's new button, drawn 13.7 x 14.2. */
export const FooterPlusIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="M8 1.7v12.6M1.95 8h12.1" /></svg>
)
export const ReloadIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="M13 8a5 5 0 1 1-1.5-3.6" /><path d="M13 2.5v3h-3" /></svg>
)
export const CloseIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="m4.5 4.5 7 7m0-7-7 7" /></svg>
)
export const PlusIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...base} {...p}><path d="M8 3.5v9M3.5 8h9" /></svg>
)
