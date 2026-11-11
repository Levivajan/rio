// Sidebar footer button of the archive module: the Library button ("View Library" with the ⇧⌘L keycaps), pressed
// while the Library is open. Resting on it shows the Library hover peek (LibraryPeek.tsx) and the tooltip.
import { IconButton } from '../../common/IconButton'
import { useCluState, useWindowId } from '../../common/useCluState'
import { LibraryIcon } from './icons'
import { libraryOpenedFromButton } from './Library'
import { libraryPeekEnter, libraryPeekLeave } from './LibraryPeek'
import { clu } from '../../common/clu'

export function ArchiveButton() {
  const windowId = useWindowId() ?? ''
  const open = useCluState((s) => s.archivePanel[windowId] === true)
  return (
    <IconButton
      aria-label="View Library"
      command="archive.open"
      aria-pressed={open}
      className="clu-archive-button"
      onPointerEnter={libraryPeekEnter}
      onPointerLeave={() => libraryPeekLeave()}
      onClick={() => {
        libraryPeekLeave(true)
        if (!open) libraryOpenedFromButton()
        void (open ? clu().archive.close() : clu().archive.open())
      }}
    >
      <LibraryIcon />
    </IconButton>
  )
}
