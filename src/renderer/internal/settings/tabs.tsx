// The app settings window's tabs (docs/chromium/parity-settings.md section 2): icon over label, in toolbar order, with
// 24 pt line icons (a gear, a person, windows, a keyboard, sliders). The hash of the page (`#shortcuts`, from settings.open) picks the tab.
import type { ReactNode } from 'react'
import { SETTINGS_TABS, type SettingsTab } from '../../../shared/ipc/data'

const icon = (children: ReactNode) => (
  <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {children}
  </svg>
)

export const TAB_LABELS: Record<SettingsTab, string> = {
  general: 'General',
  profiles: 'Profiles',
  ai: 'AI features',
  links: 'Links',
  shortcuts: 'Shortcuts',
  icon: 'Icon',
  advanced: 'Advanced'
}

export const TAB_ICONS: Record<SettingsTab, ReactNode> = {
  // A gear for General.
  general: icon(
    <>
      <path d="M18.70 10.36 L21.07 10.44 L21.07 13.56 L18.70 13.64 L17.90 15.58 L19.51 17.31 L17.31 19.51 L15.58 17.90 L13.64 18.70 L13.56 21.07 L10.44 21.07 L10.36 18.70 L8.42 17.90 L6.69 19.51 L4.49 17.31 L6.10 15.58 L5.30 13.64 L2.93 13.56 L2.93 10.44 L5.30 10.36 L6.10 8.42 L4.49 6.69 L6.69 4.49 L8.42 6.10 L10.36 5.30 L10.44 2.93 L13.56 2.93 L13.64 5.30 L15.58 6.10 L17.31 4.49 L19.51 6.69 L17.90 8.42Z" />
      <circle cx="12" cy="12" r="3" />
    </>
  ),
  profiles: icon(
    <>
      <circle cx="12" cy="8.2" r="3.7" />
      <path d="M4.6 20.2c.6-3.8 3.6-6 7.4-6s6.8 2.2 7.4 6z" />
    </>
  ),
  ai: icon(
    <>
      <path d="M10 3.5c.5 3.6 2 5.6 6 6.5-4 .9-5.5 2.9-6 6.5-.5-3.6-2-5.6-6-6.5 4-.9 5.5-2.9 6-6.5z" />
      <path d="M17.5 14c.25 1.8 1 2.8 3 3.25-2 .45-2.75 1.45-3 3.25-.25-1.8-1-2.8-3-3.25 2-.45 2.75-1.45 3-3.25z" />
    </>
  ),
  // Two stacked windows for Links.
  links: icon(
    <>
      <rect x="7.5" y="3.5" width="13" height="10" rx="2.2" />
      <path d="M7.5 6.8h13" />
      <path d="M16.5 16v1.3a2.2 2.2 0 0 1-2.2 2.2H5.7a2.2 2.2 0 0 1-2.2-2.2V10.7a2.2 2.2 0 0 1 2.2-2.2h1.8" />
    </>
  ),
  shortcuts: icon(
    <>
      <rect x="2.8" y="6" width="18.4" height="12" rx="2.4" />
      <path d="M6.5 9.6h.01M9.5 9.6h.01M12.5 9.6h.01M15.5 9.6h.01M17.5 12h.01M6.5 12h.01M8 14.6h8" />
    </>
  ),
  icon: icon(
    <>
      <rect x="4" y="4" width="16" height="16" rx="4.6" />
      <circle cx="12" cy="12" r="3.4" />
    </>
  ),
  advanced: icon(
    <>
      <path d="M4 7h9M17 7h3M4 12h3M11 12h9M4 17h11M19 17h1" />
      <circle cx="15" cy="7" r="2" />
      <circle cx="9" cy="12" r="2" />
      <circle cx="17" cy="17" r="2" />
    </>
  )
}

export function tabFromHash(hash: string): SettingsTab {
  const id = decodeURIComponent(hash.replace(/^#/, ''))
  return (SETTINGS_TABS as readonly string[]).includes(id) ? (id as SettingsTab) : 'general'
}

export { SETTINGS_TABS }
export type { SettingsTab }
