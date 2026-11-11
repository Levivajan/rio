// A Library file's right-click menu, on Media tiles and Downloads rows: Open, Show in Finder, Copy (the file),
// Copy Path, Share… (macOS's share picker at the item), Rename… and Move to Trash, in Rio's menu look. A download that
// is not a finished file in the Downloads folder has no menu. Rename edits the name in place: Enter or leaving the field
// renames, Esc keeps the old name. Cmd+C on the focused item copies the file, as the menu's Copy does.
// Near the window's bottom, as the bottom-left pop-up's rows are, the menu opens upward from the pointer.
import { useLayoutEffect, useRef, useState, type KeyboardEvent, type MouseEvent } from 'react'
import type { MenuItem } from '../../../shared/types'
import { MenuList } from '../../common/MenuList'
import { clu } from '../../common/clu'
import { invoke } from '../../common/invoke'
import { thumbTarget } from './FileThumb'
import type { LibraryItem } from './libraryModel'

const MENU_W = 200
const EDGE = 4

export const LIBRARY_ITEM_MENU: MenuItem[] = [
  { kind: 'item', id: 'open', label: 'Open' },
  { kind: 'item', id: 'reveal', label: 'Show in Finder' },
  { kind: 'separator' },
  { kind: 'item', id: 'copy', label: 'Copy' },
  { kind: 'item', id: 'copyPath', label: 'Copy Path' },
  { kind: 'item', id: 'share', label: 'Share…' },
  { kind: 'separator' },
  { kind: 'item', id: 'rename', label: 'Rename…' },
  { kind: 'item', id: 'trash', label: 'Move to Trash', destructive: true }
]

export type LibraryMenuAction = 'open' | 'reveal' | 'copy' | 'copyPath' | 'share' | 'trash' | 'rename'
type Rect = { x: number; y: number; width: number; height: number }
type Target = NonNullable<ReturnType<typeof thumbTarget>>
/** The file a menu or rename acts on, fixed when it opens, so it never follows a row that changed under it. */
type Fixed = { target: Target; name: string }

/** The menu's top at pointer y: below the pointer, or above it when that would cross the window's bottom. */
export function itemMenuTop(y: number, height: number, viewportHeight: number): number {
  if (y + height <= viewportHeight - EDGE) return y
  return Math.max(EDGE, Math.min(y - height, viewportHeight - EDGE - height))
}

/** The name Rename sends, or null when it changes nothing or cannot be a file name in that folder. */
export function renameTarget(current: string, typed: string): string | null {
  const next = typed.trim()
  if (!next || next === current || next.startsWith('.') || /[/\\:\0]/.test(next) || new TextEncoder().encode(next).length > 255) return null
  return next
}

/**
 * The menu and in-place rename of one Library item. `onAct` runs Open and Show in Finder as a click does; `onChanged`
 * reads the folder again after a rename or a move to the Trash.
 */
export function useLibraryItemMenu(item: LibraryItem, onAct: (action: 'open' | 'reveal') => void, onChanged: () => void) {
  const target = thumbTarget(item)
  const [menu, setMenu] = useState<({ x: number; y: number; anchor: Rect } & Fixed) | null>(null)
  const [renaming, setRenaming] = useState<Fixed | null>(null)
  // Enter or Esc ends the edit; the blur that follows must not rename again.
  const editing = useRef(false)
  const layer = useRef<HTMLDivElement>(null)
  const [top, setTop] = useState<number | null>(null)

  // Measured before paint, so a menu that flips never shows below the window first.
  useLayoutEffect(() => {
    if (!menu) return setTop(null)
    const h = layer.current?.querySelector('[role=menu]')?.getBoundingClientRect().height ?? 0
    setTop(itemMenuTop(menu.y, h, window.innerHeight))
  }, [menu])

  const onContextMenu = (e: MouseEvent<HTMLElement>) => {
    e.preventDefault()
    if (!target) return
    const r = e.currentTarget.getBoundingClientRect()
    // The page view covers everything right of the sidebar: the menu stays inside it.
    const right = e.currentTarget.closest('nav')?.getBoundingClientRect().right ?? window.innerWidth
    setMenu({
      x: Math.max(0, Math.min(Math.round(e.clientX), Math.floor(right) - MENU_W - 4)),
      y: Math.round(e.clientY),
      anchor: { x: Math.round(r.left), y: Math.round(r.top), width: Math.round(r.width), height: Math.round(r.height) },
      target,
      name: item.name
    })
  }

  const pick = (id: string, anchor: Rect, fixed: Fixed | null) => {
    setMenu(null)
    if (!fixed) return
    if (id === 'open' || id === 'reveal') return onAct(id)
    if (id === 'rename') {
      editing.current = true
      return setRenaming(fixed)
    }
    if (id !== 'copy' && id !== 'copyPath' && id !== 'share' && id !== 'trash') return
    void invoke(clu().download.fileAct({ ...fixed.target, action: id, ...(id === 'share' ? { anchor } : {}) })).then((r) => {
      if (r.ok && id === 'trash') onChanged()
    })
  }

  // Cmd+C on the item's own button; the rename field and the open menu keep their keys.
  const onKeyDown = (e: KeyboardEvent<HTMLElement>) => {
    if (!e.metaKey || e.shiftKey || e.altKey || e.ctrlKey || e.key.toLowerCase() !== 'c') return
    if (renaming || menu || !(e.target instanceof HTMLButtonElement)) return
    e.preventDefault()
    pick('copy', { x: 0, y: 0, width: 0, height: 0 }, target ? { target, name: item.name } : null)
  }

  const rename = (typed: string) => {
    if (!editing.current || !renaming) return
    editing.current = false
    setRenaming(null)
    const { target: file } = renaming
    const newName = renameTarget(file.name, typed)
    if (!newName) return
    void invoke(clu().download.fileAct({ ...file, action: 'rename', newName })).then((r) => {
      if (r.ok) onChanged()
    })
  }

  const menuNode = menu ? (
    <div ref={layer} className="clu-library-filter-layer" onPointerDown={(e) => e.target === e.currentTarget && setMenu(null)}>
      <MenuList
        label={menu.name}
        style={{ position: 'fixed', left: menu.x, top: top ?? menu.y, width: MENU_W }}
        items={LIBRARY_ITEM_MENU}
        onClose={() => setMenu(null)}
        onPick={(id) => pick(id, menu.anchor, menu)}
      />
    </div>
  ) : null

  const renameNode = renaming ? (
    <input
      className="clu-library-rename"
      aria-label={`Rename ${renaming.name}`}
      defaultValue={renaming.name}
      maxLength={255}
      spellCheck={false}
      autoFocus
      onFocus={(e) => {
        // As Finder does: the name without its extension is selected.
        const dot = e.currentTarget.value.lastIndexOf('.')
        e.currentTarget.setSelectionRange(0, dot > 0 ? dot : e.currentTarget.value.length)
      }}
      onKeyDown={(e) => {
        if (e.key === 'Enter') {
          e.preventDefault()
          rename(e.currentTarget.value)
        } else if (e.key === 'Escape') {
          e.preventDefault()
          e.stopPropagation()
          editing.current = false
          setRenaming(null)
        }
      }}
      onBlur={(e) => rename(e.currentTarget.value)}
    />
  ) : null

  return { onContextMenu, onKeyDown, menuNode, renameNode, renaming: renaming !== null, open: menu !== null }
}
