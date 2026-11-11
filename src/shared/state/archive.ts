// State fields of the archive module.
import type { WindowId } from '../types'

export interface ArchiveTopFields {
  archiveRevision: number
  /** Windows whose sidebar shows the archive panel instead of the tab list (02-tabs F7). */
  archivePanel: Record<WindowId, true>
}
export const defaultArchiveTop = (): ArchiveTopFields => ({ archiveRevision: 0, archivePanel: {} })
