import { FOLDER_PEEK, folderPeekRect } from '../../../shared/tabs'
import { clu } from '../../common/clu'
// Collapsed-folder popover timing: resting on a collapsed folder row for
// FOLDER_PEEK.delayMs opens its popover beside the sidebar (folders.peek); leaving the row closes it after
// FOLDER_PEEK.closeMs unless the pointer reached the popover, which holds it. A press, a toggle or a drag closes it.
// `shown` stays set after the close request on leaving: a popover the browser kept because it was held still closes on
// the next press, and resting on another folder closes this one at once (it never stays showing another
// folder).

let timer: ReturnType<typeof setTimeout> | null = null
let shown: string | null = null

const clear = () => {
  if (timer) clearTimeout(timer)
  timer = null
}

export function folderPeekEnter(folderId: string, el: HTMLElement, rows: number): void {
  if (shown !== null && shown !== folderId) folderPeekCancel()
  clear()
  // The hidden sidebar's hover peek draws its rows in a view of its own, whose coordinates are not the window's.
  if (el.closest('.clu-sidebar[data-mode="peek"]')) return
  timer = setTimeout(() => {
    timer = null
    if (!el.isConnected) return
    const row = el.getBoundingClientRect()
    const sidebar = el.closest('.clu-sidebar')?.getBoundingClientRect()
    shown = folderId
    void clu().folders.peek({ folderId, rect: folderPeekRect(row, sidebar ? sidebar.right : row.right, window.innerHeight, rows) })
  }, FOLDER_PEEK.delayMs)
}

export function folderPeekLeave(): void {
  clear()
  if (shown === null) return
  timer = setTimeout(() => {
    timer = null
    void clu().folders.peek({ folderId: null })
  }, FOLDER_PEEK.closeMs)
}

/** Closes it now (a press on a row, the folder opening, another folder row hovered). */
export function folderPeekCancel(): void {
  clear()
  if (shown === null) return
  const folderId = shown
  shown = null
  void clu().folders.peek({ folderId, held: false })
}
