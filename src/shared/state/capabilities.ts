// State fields of the capabilities module. `prompts` is shared with engine and platform.
import type { TabId } from '../types'

export interface PromptState {
  id: string
  tabId: TabId
  kind: 'permission' | 'passkey' | 'auth' | 'leave' | 'client-cert' | 'external-app'
  origin: string
  items: string[]
  app?: string
}
export interface CapabilitiesWindowFields { prompts: PromptState[] }
export const defaultCapabilitiesWindow = (): CapabilitiesWindowFields => ({ prompts: [] })

export type DownloadPhase = 'progressing' | 'paused' | 'completed' | 'cancelled' | 'interrupted'
export interface DownloadState {
  id: string
  profileId: string
  filename: string
  url: string
  state: DownloadPhase
  receivedBytes: number
  /** 0 when the server sent no length (indeterminate). */
  totalBytes: number
  /** Smoothed bytes per second while progressing, else 0. */
  bytesPerSecond: number
  /** Milliseconds left, null when unknown (indeterminate total or no rate yet). */
  etaMs: number | null
  startedAt: number
  endedAt: number | null
  /** Completed rows: false once the file is gone from disk ("Removed"). */
  exists: boolean
}
/** A screenshot or screen recording that landed in the screenshot folder while Rio runs, newest first. */
export interface ScreenshotArrival {
  /** The bare file name; download.fileAct opens it with source media. */
  name: string
  kind: 'image' | 'video'
  /** When the file was written, ms since the epoch. */
  at: number
}
export interface CapabilitiesTopFields { downloads: DownloadState[]; screenshotArrivals: ScreenshotArrival[] }
export const defaultCapabilitiesTop = (): CapabilitiesTopFields => ({ downloads: [], screenshotArrivals: [] })
