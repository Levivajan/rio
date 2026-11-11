// State fields of the data module.
import type { Json, ProfileId } from '../types'

export interface DataAppFields {
  dataHealth: { recovered: 'backup' | 'fresh' | null; persistent: boolean }
  import: { source: 'chrome' | 'arc'; phase: string; done: number; total: number; skipped: number } | null
}
export const defaultDataApp = (): DataAppFields => ({ dataHealth: { recovered: null, persistent: true }, import: null })

export interface DataTopFields {
  settings: { global: Record<string, Json>; byProfile: Record<ProfileId, Record<string, Json>> }
  historyRevision: number
}
export const defaultDataTop = (): DataTopFields => ({ settings: { global: {}, byProfile: {} }, historyRevision: 0 })
