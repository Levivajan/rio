// Every file the Library lists (Media tiles, Downloads rows, the hover peek and the bottom-left arrivals)
// drags out as the file itself, as chrome://downloads' rows do: the dragstart is cancelled, so no HTML drag of the
// button or its thumbnail starts, and the browser starts the native file drag under the pointer instead
// (download.act or download.fileAct with action drag). Finder copies the file, other apps and pages take it as an
// upload. A download drags by its id once it is finished and on disk; a folder file by its source and bare name, which
// the browser resolves inside that folder. Anything else starts no drag.
import type { DragEvent } from 'react'
import type { LibrarySource } from '../../../shared/ipc/capabilities'
import { clu } from '../../common/clu'
import type { LibraryItem } from './libraryModel'

export type FileDragTarget = { downloadId: string } | { source: LibrarySource; name: string }

/** What a Library item drags as, or null when it has no file to drag (a running, failed or removed download). */
export function dragTargetOf(item: LibraryItem): FileDragTarget | null {
  if (item.type === 'file') return { source: item.source, name: item.name }
  return item.download.state === 'completed' && item.download.exists ? { downloadId: item.download.id } : null
}

/** The dragstart handler: cancels the HTML drag and asks the browser for the file's drag. */
export function startFileDrag(e: DragEvent, target: FileDragTarget | null): void {
  e.preventDefault()
  if (!target) return
  if ('downloadId' in target) void clu().download.act({ id: target.downloadId, action: 'drag' })
  else void clu().download.fileAct({ source: target.source, name: target.name, action: 'drag' })
}
