// Settings registry. Lanes add keys inside their own block.
import { APP_ICONS, DEFAULT_APP_ICON } from './appIcons'
import { isCustomShortcuts } from './shortcuts'
import type { Json } from './types'

export type SettingScope = 'global' | 'profile'
export interface SettingDef<T extends Json = Json> {
  key: string
  scope: SettingScope
  default: T
  /** Returns true when the stored value is valid; invalid values read as the default. */
  validate: (value: unknown) => value is T
}

const oneOf =
  <T extends string | number>(...values: T[]) =>
  (v: unknown): v is T =>
    (values as unknown[]).includes(v)
const isBool = (v: unknown): v is boolean => typeof v === 'boolean'
const intIn =
  (min: number, max: number) =>
  (v: unknown): v is number =>
    typeof v === 'number' && Number.isInteger(v) && v >= min && v <= max

export const settingsRegistry: readonly SettingDef[] = [
  // #region clu:theme (edit only inside this block)
  { key: 'appearance.mode', scope: 'global', default: 'auto', validate: oneOf('auto', 'light', 'dark') },
  {
    key: 'onboarding.completedAt',
    scope: 'global',
    default: null,
    // Junk counts as completed (PLAN 7.3), so any stored JSON value (objects and arrays too) is kept as-is.
    validate: (v: unknown): v is Json => v !== undefined
  },
  // #endregion clu:theme
  // #region clu:settings (edit only inside this block)
  // The app settings window (docs/chromium/parity-settings.md): the AI features, the Links tab, custom shortcuts.
  { key: 'ai.tidyTabs', scope: 'global', default: true, validate: isBool },
  { key: 'ai.instantLinks', scope: 'global', default: true, validate: isBool },
  { key: 'ai.hoverPreviews', scope: 'global', default: true, validate: isBool },
  { key: 'ai.tidyTitles', scope: 'global', default: true, validate: isBool },
  { key: 'ai.tidyDownloads', scope: 'global', default: true, validate: isBool },
  { key: 'ai.askRow', scope: 'global', default: true, validate: isBool },
  { key: 'ai.askAssistant', scope: 'global', default: 'chatgpt', validate: oneOf('chatgpt', 'claude', 'grok') },
  { key: 'links.miniFromOtherApps', scope: 'global', default: true, validate: isBool },
  // General, Automatically update Rio: the updater's hourly checks and silent downloads, on by default.
  { key: 'general.autoUpdate', scope: 'global', default: true, validate: isBool },
  // Advanced, Enable Site tweaks on websites you visit: off, no page shows a saved tweak, on by default.
  { key: 'tweaks.enabled', scope: 'global', default: true, validate: isBool },
  // Advanced, Show full URL when Toolbar is enabled: the toolbar's pill shows the whole URL, not the host; off by default.
  { key: 'toolbar.fullUrl', scope: 'global', default: false, validate: isBool },
  // Advanced, Haptic feedback when reordering tabs: a sidebar drop that moves tabs ticks the trackpad, on by default.
  { key: 'tabs.reorderHaptic', scope: 'global', default: true, validate: isBool },
  // General, Warn before quitting: ⌘Q asks to be held, on by default.
  { key: 'general.warnBeforeQuit', scope: 'global', default: true, validate: isBool },
  // General, Picture in picture when leaving a tab: a playing video pops out when its tab stops showing, on by default.
  { key: 'general.autoPictureInPicture', scope: 'global', default: true, validate: isBool },
  { key: 'links.peekOnShiftClick', scope: 'global', default: true, validate: isBool },
  { key: 'links.peekOtherSites', scope: 'global', default: true, validate: isBool },
  { key: 'shortcuts.custom', scope: 'global', default: {}, validate: isCustomShortcuts },
  // #endregion clu:settings
  // #region clu:shell (edit only inside this block)
  { key: 'shell.lastSidebarWidth', scope: 'global', default: 214, validate: intIn(164, 500) },
  // #endregion clu:shell
  // #region clu:archive (edit only inside this block)
  { key: 'tabs.archiveAfterHours', scope: 'profile', default: 12, validate: oneOf(12, 24, 168, 720) },
  // #endregion clu:archive
  // #region clu:command (edit only inside this block)
  { key: 'search.engineId', scope: 'profile', default: 'google', validate: oneOf('google', 'duckduckgo', 'bing', 'brave', 'ecosia') },
  // #endregion clu:command
  // #region clu:icons (edit only inside this block)
  { key: 'appearance.appIcon', scope: 'global', default: DEFAULT_APP_ICON, validate: oneOf(...APP_ICONS.map((i) => i.id)) }
  // #endregion clu:icons
]

const defs = new Map(settingsRegistry.map((d) => [d.key, d]))
export function settingDef(key: string): SettingDef | undefined {
  return defs.get(key)
}

/** The stored value when it is valid for the key, else the default. Unknown keys give undefined. */
export function readSetting(key: string, stored: unknown): Json | undefined {
  const def = defs.get(key)
  if (!def) return undefined
  return stored !== undefined && def.validate(stored) ? (stored as Json) : def.default
}
