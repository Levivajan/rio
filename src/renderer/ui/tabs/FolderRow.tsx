// Folder row in Pinned (02-tabs.md F3): the folder glyph (closed or open, in the space
// accent) or a custom icon in the favicon slot, then the name in semibold; no disclosure chevron, the glyph shows the
// state. A click anywhere on it, its glyph included, toggles it; only a right-click opens the folder
// menu. A double-click renames it inline, and a press past 4 px drags it (F8). On hover a minus button near
// the right edge collapses an open folder; resting on a collapsed folder opens its popover beside the sidebar
// (folderPeek.ts), and resting on any other folder row closes that popover.
import { useRef, type CSSProperties, type PointerEvent as ReactPointerEvent, type SVGProps } from 'react'
import type { FolderState } from '../../../shared/state'
import { folderPeekTabs } from '../../../shared/tabs'
import { IconButton } from '../../common/IconButton'
import { Tooltip } from '../../common/Tooltip'
import { TreeRow } from '../../common/TreeRow'
import { useCluState } from '../../common/useCluState'
import { folderPeekCancel, folderPeekEnter, folderPeekLeave } from './folderPeek'
import { hoverLeave } from './hoverCard'
import { RenameField } from './RenameField'
import { openRowMenu, setRenaming, useRenaming } from './renaming'
import { pressRow } from './useSidebarDrag'
import { clu } from '../../common/clu'

const glyph = { fill: 'none', stroke: 'currentColor', strokeWidth: 1.5, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const CLOSED = (
  <>
    <path d="M1.75 4.25a2 2 0 0 1 2-2h3.6l2 2.25h5.9a2 2 0 0 1 2 2v8.25a2 2 0 0 1-2 2H3.75a2 2 0 0 1-2-2Z" />
    <path d="M1.75 7.25h15.5" />
  </>
)
/** Closed: 19 x 19, its body filled light. Open: 22.5 x 16.5. Both centered on the favicon slot. */
const FolderIcon = ({ open, ...p }: SVGProps<SVGSVGElement> & { open: boolean }) =>
  open ? (
    <svg {...glyph} width={23} height={17} viewBox="0 0 23 17" {...p}>
      <path d="M1.75 13.5V2.75A1.25 1.25 0 0 1 3 1.5h4.2l1.8 2h8a1.25 1.25 0 0 1 1.25 1.25V6.5" />
      <path d="M1.75 13.5 4.4 7.4a1.5 1.5 0 0 1 1.4-.9h14.6a.9.9 0 0 1 .8 1.3l-2.7 6.3a1.5 1.5 0 0 1-1.4.9H3a1.25 1.25 0 0 1-1.25-1.5Z" />
    </svg>
  ) : (
    <svg {...glyph} width={19} height={19} viewBox="0 0 19 19" {...p}>
      {CLOSED}
    </svg>
  )
/** The collapse button's minus: 11.5 wide and 2 thick. */
const CollapseIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={16} height={16} viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth={2} strokeLinecap="round" aria-hidden="true" {...p}>
    <path d="M3.25 8h9.5" />
  </svg>
)
const keepPointer = (e: ReactPointerEvent) => e.stopPropagation()

/** Rows indent --folder-indent (16) per folder level, the whole row box with them: a child row's hover and selected
 *  fill starts 16 in. --row-indent lets the drop band still span the sidebar. */
export const indent = (level: number) => {
  const by = `calc(${level - 1} * var(--folder-indent))`
  return { marginInlineStart: by, '--row-indent': by } as CSSProperties
}

export function FolderRow({ folder, level, dragEnabled = true }: { folder: FolderState; level: number; dragEnabled?: boolean }) {
  const setCollapsed = (collapsed: boolean) => {
    folderPeekCancel()
    void clu().folders.setCollapsed({ folderId: folder.id, collapsed })
  }
  const peekRows = useCluState((s) => folderPeekTabs(s, folder.id).length)
  const toggle = () => setCollapsed(!folder.collapsed)
  // A double-click renames: its first click toggled, the second is ignored, and the folder goes back as it was.
  const before = useRef(folder.collapsed)
  const renaming = useRenaming(folder.id)
  // An empty or unchanged name leaves the folder as it was.
  const rename = (next: string | null) => {
    setRenaming(null)
    if (next && next !== folder.name) void clu().tabs.rename({ nodeId: folder.id, title: next })
  }
  return (
    <TreeRow
      selected={false}
      level={level}
      expanded={!folder.collapsed}
      data-folder-id={folder.id}
      data-collapsed={String(folder.collapsed)}
      data-renaming={String(renaming)}
      data-custom-icon={folder.icon ? 'true' : 'false'}
      className="clu-folder-row"
      style={indent(level)}
      onClick={(e) => {
        if (e.detail > 1) return
        before.current = folder.collapsed
        toggle()
      }}
      onPointerEnter={(e) => {
        if (folder.collapsed && !renaming) folderPeekEnter(folder.id, e.currentTarget, peekRows)
        else folderPeekCancel()
      }}
      onPointerLeave={folderPeekLeave}
      onPointerDown={(e) => {
        hoverLeave()
        folderPeekCancel()
        if (dragEnabled) pressRow(e, folder.id, [])
      }}
      onDoubleClick={() => {
        setCollapsed(before.current)
        setRenaming(folder.id)
      }}
      onContextMenu={(e) => {
        e.preventDefault()
        void openRowMenu(folder.id, e.clientX, e.clientY)
      }}
      onKeyDown={(e) => {
        if (e.key === 'Enter' || (e.key === 'ArrowLeft' && !folder.collapsed) || (e.key === 'ArrowRight' && folder.collapsed)) toggle()
      }}
    >
      {folder.icon ? (
        <span key={folder.icon} className="clu-row-favicon clu-row-emoji" aria-hidden="true">
          {folder.icon}
        </span>
      ) : (
        <span className="clu-folder-glyph" data-open={String(!folder.collapsed)}>
          <FolderIcon open={!folder.collapsed} />
        </span>
      )}
      {renaming ? (
        <RenameField value={folder.name} label="Folder name" onDone={rename} />
      ) : folder.collapsed ? (
        // A collapsed folder's popover says what it holds; an open one shows its name in an accent pill on hover.
        <span className="clu-row-title">{folder.name}</span>
      ) : (
        <Tooltip label={folder.name}>
          <span className="clu-row-title">{folder.name}</span>
        </Tooltip>
      )}
      {folder.collapsed || renaming ? null : (
        <IconButton
          aria-label="Collapse folder"
          className="clu-row-close clu-folder-collapse"
          tabIndex={-1}
          onPointerDown={keepPointer}
          onDoubleClick={(e) => e.stopPropagation()}
          onClick={(e) => {
            e.stopPropagation()
            setCollapsed(true)
          }}
        >
          <CollapseIcon />
        </IconButton>
      )}
    </TreeRow>
  )
}
