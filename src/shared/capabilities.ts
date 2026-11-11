// Capabilities types shared by main and renderers.
import type { DownloadState } from './state/capabilities'

export type { DownloadPhase, DownloadState, PromptState } from './state/capabilities'
export type { PermissionRow, HibernationRow } from './ipc/capabilities'
/** `live` counts every live page; `background` only live tab pages no window shows, the ones `liveCap` limits. */
export interface HibernationSnapshot { liveCap: number; live: number; background: number; hibernated: number }

/** What a site can be allowed or blocked, as stored in site_permissions and shown in Site permissions. */
export const SITE_PERMISSIONS = ['camera', 'microphone', 'location', 'notifications', 'clipboard', 'popups'] as const
export type SitePermission = (typeof SITE_PERMISSIONS)[number]
/**
 * The other kinds a site can have a decision for in Chromium's site settings, in the order permission.list returns
 * them (kKinds in chromium/clu/browser/capabilities/clu_site_permissions.cc). Site controls shows one only once the
 * site has a decision for it.
 */
export const OTHER_PERMISSION_LABELS: Record<string, string> = {
  sound: 'Sound',
  automaticDownloads: 'Automatic downloads',
  midi: 'MIDI devices',
  sensors: 'Motion sensors',
  cameraPanTiltZoom: 'Camera pan and zoom',
  windowManagement: 'Window management',
  localFonts: 'Fonts on your computer',
  idleDetection: 'Your device use',
  backgroundSync: 'Background sync',
  javascript: 'JavaScript',
  images: 'Images',
  vr: 'Virtual reality',
  ar: 'Augmented reality',
  handTracking: 'Hand tracking',
  keyboardLock: 'Keyboard lock',
  pointerLock: 'Pointer lock',
  capturedSurfaceControl: 'Scroll and zoom shared tabs',
  localNetworkAccess: 'Local network access',
  autoPictureInPicture: 'Automatic picture-in-picture'
}

/** Every permission name permission.set and permission.clear take. */
export const KNOWN_PERMISSIONS: readonly string[] = [...SITE_PERMISSIONS, ...Object.keys(OTHER_PERMISSION_LABELS)]

/** The label of a permission row; a name this build does not know shows as itself. */
export function permissionLabel(permission: string): string {
  if (isSitePermission(permission)) return SITE_PERMISSION_LABELS[permission]
  return Object.hasOwn(OTHER_PERMISSION_LABELS, permission) ? (OTHER_PERMISSION_LABELS[permission] ?? permission) : permission
}

/** Prompt items: the stored kinds plus screen sharing, which asks every time and is never stored. */
export type PromptItem = SitePermission | 'screen'
export const isSitePermission = (v: unknown): v is SitePermission => typeof v === 'string' && (SITE_PERMISSIONS as readonly string[]).includes(v)

export const SITE_PERMISSION_LABELS: Record<SitePermission, string> = {
  camera: 'Camera',
  microphone: 'Microphone',
  location: 'Location',
  notifications: 'Notifications',
  clipboard: 'Clipboard',
  popups: 'Pop-ups'
}

const PROMPT_PHRASES: Record<PromptItem, string> = {
  camera: 'use your camera',
  microphone: 'use your microphone',
  location: 'know your location',
  notifications: 'show notifications',
  clipboard: 'see text and images you copy',
  popups: 'open pop-up windows',
  screen: 'share your screen'
}

/** "example.com wants to use your camera and microphone". Camera plus microphone reads as one phrase. */
export function promptText(host: string, items: readonly string[]): string {
  const known = [...new Set(items)].filter((i): i is PromptItem => i in PROMPT_PHRASES)
  const both = known.includes('camera') && known.includes('microphone')
  const phrases: string[] = []
  for (const item of known) {
    const av = item === 'camera' || item === 'microphone'
    if (!both || !av) phrases.push(PROMPT_PHRASES[item])
    else if (!phrases.includes('use your camera and microphone')) phrases.push('use your camera and microphone')
  }
  const list = phrases.length <= 2 ? phrases.join(' and ') : `${phrases.slice(0, -1).join(', ')} and ${phrases.at(-1)}`
  return `${host} wants to ${list || 'use a device'}`
}

/** The host a prompt or settings row shows for an origin; the origin itself when it does not parse. */
export function originHost(origin: string): string {
  try {
    return new URL(origin).hostname || origin
  } catch {
    return origin
  }
}

/**
 * The downloads one window lists: a private window shows only its own profile's, and other windows never show a
 * private profile's.
 */
export function downloadsForWindow<T extends { profileId: string }>(
  downloads: readonly T[],
  win: { private: boolean; activeSpaceId: string } | undefined,
  spaces: Readonly<Record<string, { profileId: string }>>,
  profiles: Readonly<Record<string, { ephemeral: boolean }>>
): T[] {
  if (win?.private) {
    const own = spaces[win.activeSpaceId]?.profileId
    return downloads.filter((d) => d.profileId === own)
  }
  return downloads.filter((d) => !profiles[d.profileId]?.ephemeral)
}

/** Live downloads: the sidebar shows a progress ring and the popover shows Pause or Resume and Cancel. */
export const isActiveDownload = (state: string): boolean => state === 'progressing' || state === 'paused'

/** Decimal units, as Finder shows them: 980 bytes, 12.4 kB, 80 MB, 1.2 GB. */
export function formatBytes(n: number): string {
  if (!Number.isFinite(n) || n < 0) return '0 bytes'
  if (n < 1000) return `${Math.round(n)} ${Math.round(n) === 1 ? 'byte' : 'bytes'}`
  const units = ['kB', 'MB', 'GB', 'TB']
  let v = n / 1000
  let i = 0
  while (v >= 1000 && i < units.length - 1) {
    v /= 1000
    i += 1
  }
  const digits = v >= 100 ? 0 : 1
  return `${v.toFixed(digits).replace(/\.0$/, '')} ${units[i]}`
}

/** "8 s left", "1 min left", "2 h left"; null when unknown. */
export function formatEta(ms: number | null): string | null {
  if (ms === null || !Number.isFinite(ms) || ms < 0) return null
  const s = Math.ceil(ms / 1000)
  if (s < 60) return `${Math.max(1, s)} s left`
  const min = Math.round(s / 60)
  if (min < 60) return `${min} min left`
  return `${Math.round(min / 60)} h left`
}

/** The status line of a downloads row: "12.4 MB of 80 MB, 1 min left". */
export function downloadStatus(d: Pick<DownloadState, 'state' | 'receivedBytes' | 'totalBytes' | 'etaMs' | 'exists'>): string {
  const sizes = d.totalBytes > 0 ? `${formatBytes(d.receivedBytes)} of ${formatBytes(d.totalBytes)}` : formatBytes(d.receivedBytes)
  switch (d.state) {
    case 'progressing': {
      const eta = d.totalBytes > 0 ? formatEta(d.etaMs) : null
      return eta ? `${sizes}, ${eta}` : sizes
    }
    case 'paused':
      return `Paused, ${sizes}`
    case 'completed':
      return d.exists ? formatBytes(d.totalBytes || d.receivedBytes) : 'Removed'
    case 'cancelled':
      return 'Cancelled'
    case 'interrupted':
      return `Failed, ${sizes}`
  }
}
