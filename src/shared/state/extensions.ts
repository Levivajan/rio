// State fields of the extensions module.
import type { ExtensionInfo } from '../ipc/extensions'
import type { CluState } from '../state'

/** `devMode`: the site is in Developer Mode, so the page card shows the developer strip above it. */
export interface ExtensionsTabFields { site: string | null; blockedCount: number; blockingEnabled: boolean; devMode: boolean }
export const defaultExtensionsTab = (): ExtensionsTabFields => ({ site: null, blockedCount: 0, blockingEnabled: true, devMode: false })

export interface ExtensionsProfileFields { extensions: ExtensionInfo[] }
export const defaultExtensionsProfile = (): ExtensionsProfileFields => ({ extensions: [] })

/** One extension's action for the page the window shows (Chromium's toolbar action): `icon` is a PNG data URL with the
 * badge drawn in, `badge` its text for screen readers, `enabled` false where the action does nothing on this page. */
export interface ActionButton { id: string; name: string; title: string; icon: string | null; badge: string; enabled: boolean; pinned: boolean }
/** What a window's action row and extensions menu show, in the profile's pin order then by name; null hides the row
 * (the profile has no extension). */
export interface ActionRowState { profileId: string; actions: ActionButton[] }

export interface ExtensionsWindowFields {
  installPrompt: { requestId: string; name: string; iconUrl: string | null; permissions: string[]; warnUnsupported: boolean } | null
  actionRow: ActionRowState | null
  /** A Mini window's page fields (its page is not a tab); null in a main window. */
  miniPage: ExtensionsTabFields | null
}
export const defaultExtensionsWindow = (): ExtensionsWindowFields => ({ installPrompt: null, actionRow: null, miniPage: null })

export type SiteControlsPage = { id: string; url: string } & ExtensionsTabFields

/** The page Site controls act on: a main window's active tab, or a Mini window's page. */
export function siteControlsPage(s: CluState, windowId: string): SiteControlsPage | null {
  const w = s.windows[windowId]
  if (!w) return null
  if (w.mini) return { id: w.mini.tabId, url: w.mini.url, ...(w.miniPage ?? defaultExtensionsTab()) }
  const tab = w.activeTabId ? s.tabs[w.activeTabId] : undefined
  return tab ? { id: tab.id, url: tab.url, site: tab.site, blockedCount: tab.blockedCount, blockingEnabled: tab.blockingEnabled, devMode: tab.devMode } : null
}
