// Rio's app icons (docs/chromium/icons.md): the default and the six alternates the App icon setting picks from, in the
// picker's order. An alternate's id is its artwork's name (assets/brand/app-icons/rio-<id>.svg); the default is
// assets/brand/clu-icon.svg. chromium/clu/browser/app_icon keeps the same ids.
export const APP_ICONS = [
  { id: 'flatWarm', label: 'Flat warm' },
  { id: 'airySun', label: 'Sunset' },
  { id: 'ocean', label: 'Ocean' },
  { id: 'flatBlue', label: 'Flat blue' },
  { id: 'sunset', label: 'Rio sunset' },
  { id: 'warm', label: 'Warm' },
  { id: 'dusk', label: 'Dusk' }
] as const

export type AppIconId = (typeof APP_ICONS)[number]['id']
export const DEFAULT_APP_ICON: AppIconId = 'flatWarm'
