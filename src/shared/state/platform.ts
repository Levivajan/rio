// State fields of the platform module.

/**
 * Sparkle updates (docs/chromium/m12.md). `unavailable` means this build has no update feed (a dev build, or a
 * package made without one). `ready` stays until Clu relaunches: the update installs on quit or on Relaunch.
 */
export type UpdateStatus = 'unavailable' | 'idle' | 'checking' | 'downloading' | 'ready' | 'up-to-date' | 'error'
export interface UpdateState {
  status: UpdateStatus
  /** The version found or ready, as the feed names it; null before one is found. */
  version: string | null
  /** Epoch ms of the last finished check, or null. */
  checkedAt: number | null
  /** The user closed the update card for this ready version; it still installs on quit. */
  dismissed: boolean
}
export const defaultUpdateState = (): UpdateState => ({ status: 'unavailable', version: null, checkedAt: null, dismissed: false })

export interface PlatformAppFields {
  defaultBrowser: 'default' | 'not-default' | 'unavailable'
  update: UpdateState
}
export const defaultPlatformApp = (): PlatformAppFields => ({
  defaultBrowser: 'unavailable',
  update: defaultUpdateState()
})
