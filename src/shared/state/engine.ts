// State fields of the engine module.
export type PageIssue =
  | { kind: 'load-failed'; code: number; name: string; url: string }
  | { kind: 'certificate'; code: number; name: string; url: string; host: string; bypassable: boolean }
  | { kind: 'crashed'; reason: 'crashed' | 'oom' | 'killed' | 'launch-failed' | 'other'; repeated: boolean }

export interface PageFields {
  lifecycle: 'unloaded' | 'live' | 'hibernated' | 'restoring'
  pendingUrl: string | null
  loading: boolean
  progress: number
  canGoBack: boolean
  canGoForward: boolean
  audible: boolean
  zoomFactor: number
  find: { text: string; active: number; total: number } | null
  issue: PageIssue | null
  security: 'secure' | 'insecure' | 'bypassed' | 'none'
  fullscreen: boolean
  unresponsive: boolean
}

export function defaultPageFields(): PageFields {
  return {
    lifecycle: 'unloaded', pendingUrl: null, loading: false, progress: 0, canGoBack: false, canGoForward: false, audible: false,
    zoomFactor: 1, find: null, issue: null, security: 'none', fullscreen: false, unresponsive: false
  }
}

export interface EngineWindowFields { peekPage: PageFields | null }
export const defaultEngineWindow = (): EngineWindowFields => ({ peekPage: null })
