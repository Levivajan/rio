// Sidebar toggle icon: Clu's own line icon, a panel with its left column and three short item lines, drawn
// 18.7 x 14.2 at stroke 1.5.
import type { SVGProps } from 'react'

export const SidebarIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={20} height={20} viewBox="0 0 20 20" fill="none" stroke="currentColor" strokeWidth={1.5} strokeLinecap="round" strokeLinejoin="round" aria-hidden {...p}>
    <rect x="1.15" y="3.65" width="17.2" height="12.7" rx="3.2" />
    <path d="M7.75 3.65v12.7" />
    <path d="M3.9 7h1.6M3.9 9h1.6M3.9 11h1.6" strokeWidth={1.1} />
  </svg>
)
