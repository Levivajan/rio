// The Default browser row in General (docs/chromium/fixes.md): what it says and whether its button asks macOS.
import type { PlatformAppFields } from '../../../../shared/state/platform'

/** The last request's answer: platform.requestDefaultBrowser's status, 'failed' for an error, or null. */
export type DefaultBrowserAsk = 'requested' | 'already-default' | 'not-packaged' | 'failed' | null

export interface DefaultBrowserRow {
  text: string
  /** The button shows while Rio isn't the default and this build can ask. */
  canAsk: boolean
}

export function defaultBrowserRow(state: PlatformAppFields['defaultBrowser'], ask: DefaultBrowserAsk): DefaultBrowserRow {
  if (state === 'default') return { text: 'Rio is your default browser.', canAsk: false }
  if (ask === 'not-packaged') return { text: 'Install Rio in Applications to make it your default browser.', canAsk: false }
  if (ask === 'requested') return { text: 'Confirm the change in the dialog macOS shows.', canAsk: true }
  if (ask === 'failed') return { text: 'Rio couldn’t ask macOS. Try again, or pick Rio in System Settings, Desktop & Dock.', canAsk: true }
  if (state === 'not-default') return { text: 'Rio isn’t your default browser.', canAsk: true }
  // Before the first check answers, and in a build macOS can't name.
  return { text: 'Links from other apps open in Rio once it is your default browser.', canAsk: true }
}
