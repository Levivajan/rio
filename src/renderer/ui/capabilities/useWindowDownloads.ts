// The downloads one window lists: the sidebar button and the popover read the same rows.
import { useCallback } from 'react'
import { downloadsForWindow, type DownloadState } from '../../../shared/capabilities'
import type { CluState } from '../../../shared/state'
import { useCluState } from '../../common/useCluState'

const sameRows = (a: DownloadState[], b: DownloadState[]) => a.length === b.length && a.every((d, i) => d === b[i])

/** The downloads this window lists (downloadsForWindow), stable while they do not change. */
export function useWindowDownloads(windowId: string | null): DownloadState[] {
  const select = useCallback(
    (s: CluState) => downloadsForWindow(s.downloads ?? [], windowId ? s.windows[windowId] : undefined, s.spaces, s.profiles),
    [windowId]
  )
  return useCluState(select, sameRows)
}
