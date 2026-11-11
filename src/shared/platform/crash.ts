// Crash notice copy and size shared by main and the overlay (docs/plan/09-platform.md F14).
export const RESTORED_NOTICE = 'Rio quit unexpectedly. Your windows and tabs were restored.'
/** The passive overlay view that shows it: bottom center of the page, 10 s. */
export const RESTORED_NOTICE_SIZE = { width: 440, height: 84 } as const
export const RESTORED_NOTICE_MS = 10_000
