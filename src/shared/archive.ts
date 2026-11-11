// Archive types shared by main and renderers (seeded by the scaffold).
import type { NavState } from './navState'
export type { ArchiveRow } from './ipc/archive'
export const ARCHIVE_REASONS = ['close', 'clear', 'auto', 'delete', 'mini'] as const
export type ArchiveReason = (typeof ARCHIVE_REASONS)[number]
export function isArchiveReason(v: unknown): v is ArchiveReason {
  return typeof v === 'string' && (ARCHIVE_REASONS as readonly string[]).includes(v)
}
/** The four "Archive tabs after" choices in hours (PLAN 7.3); any other stored value reads as the first. */
export const ARCHIVE_AFTER_HOURS = [12, 24, 168, 720] as const
export const DEFAULT_ARCHIVE_AFTER_HOURS = 12
/** Retention (PLAN decision 17): rows older than 90 days, or beyond 5000 per profile, are pruned in the sweep. */
export const ARCHIVE_RETENTION_MS = 90 * 24 * 60 * 60 * 1000
export const ARCHIVE_MAX_ROWS = 5000
export interface TabSnapshot {
  tabId: string
  profileId: string
  spaceId: string | null
  title: string
  url: string
  faviconId: string | null
  navState: NavState | null
  splitId: string | null
}
