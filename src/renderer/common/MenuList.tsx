// Menu list: role menu, menuitem rows, aria-haspopup on submenu parents.
// Two submenu levels; a submenu flips left when it would cross the right edge and up near the bottom.
import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties } from 'react'
import { LOOK } from '../../shared/look/tokens'
import type { MenuItem } from '../../shared/types'

type Item = Extract<MenuItem, { kind: 'item' }>

/** The menu's top and bottom padding: a submenu rises by it so its first item lines up with the parent item. */
const MENU_PAD_Y = LOOK.menu.paddingY

/** Where a submenu goes: right of its parent row unless that crosses the viewport, its first item level with the
 *  parent item. Pure, unit tested. */
export function placeSubmenu(parent: { left: number; right: number; top: number }, size: { width: number; height: number }, viewport: { width: number; height: number }) {
  const flipLeft = parent.right + size.width > viewport.width
  const left = flipLeft ? Math.max(0, parent.left - size.width) : parent.right
  const top = Math.max(0, Math.min(parent.top - MENU_PAD_Y, viewport.height - size.height))
  return { left, top, flipLeft }
}

const Chevron = () => (
  <svg width="7" height="11" viewBox="0 0 7 11" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d="m1.5 1.5 4 4-4 4" />
  </svg>
)

export function MenuList({ items, onPick, onClose, depth = 0, style, label, focusFirst = true, closing = false }: { items: MenuItem[]; onPick: (id: string) => void; onClose: () => void; depth?: number; style?: CSSProperties; label?: string; focusFirst?: boolean; closing?: boolean }) {
  const ref = useRef<HTMLDivElement>(null)
  // A submenu the pointer opened highlights nothing until the pointer or a key reaches it, as the native one does.
  const [focus, setFocus] = useState(() => (focusFirst ? items.findIndex((i) => i.kind === 'item' && i.enabled !== false) : -1))
  const [openSub, setOpenSub] = useState<number | null>(null)
  const [subByKey, setSubByKey] = useState(false)
  const [subPos, setSubPos] = useState<{ left: number; top: number; flipLeft: boolean } | null>(null)
  const rowRefs = useRef<(HTMLDivElement | null)[]>([])

  useEffect(() => {
    if (depth === 0 || focus < 0) ref.current?.focus()
    // eslint-disable-next-line react-hooks/exhaustive-deps -- once, when the list opens
  }, [depth])
  useEffect(() => {
    if (focus >= 0) rowRefs.current[focus]?.focus()
  }, [focus])

  const enabledIndexes = items.map((it, i) => (it.kind === 'item' && it.enabled !== false ? i : -1)).filter((i) => i >= 0)
  const move = (dir: 1 | -1) => {
    if (!enabledIndexes.length) return
    const at = enabledIndexes.indexOf(focus)
    const next = at < 0 ? (dir === 1 ? 0 : enabledIndexes.length - 1) : (at + dir + enabledIndexes.length) % enabledIndexes.length
    setFocus(enabledIndexes[next] ?? focus)
  }

  const activate = (i: number, byKey = false) => {
    const it = items[i]
    if (!it || it.kind !== 'item' || it.enabled === false) return
    if (it.submenu?.length) {
      setSubByKey(byKey)
      setOpenSub(i)
    } else onPick(it.id)
  }

  useLayoutEffect(() => {
    if (openSub === null) return setSubPos(null)
    const row = rowRefs.current[openSub]?.getBoundingClientRect()
    if (!row) return
    const sub = items[openSub]
    const count = sub && sub.kind === 'item' ? (sub.submenu?.length ?? 0) : 0
    setSubPos(placeSubmenu(row, { width: 220, height: count * 28 + 8 }, { width: window.innerWidth, height: window.innerHeight }))
  }, [openSub, items])

  const sub = openSub !== null ? items[openSub] : undefined
  return (
    <div
      ref={ref}
      role="menu"
      aria-label={label ?? 'Menu'}
      tabIndex={-1}
      className="clu-menu"
      style={style}
      data-depth={depth}
      data-closing={closing || undefined}
      onKeyDown={(e) => {
        if (e.key === 'ArrowDown') move(1)
        else if (e.key === 'ArrowUp') move(-1)
        else if (e.key === 'Enter' || e.key === ' ') activate(focus, true)
        else if (e.key === 'ArrowRight') activate(focus, true)
        else if (e.key === 'Escape' || e.key === 'ArrowLeft') {
          if (depth === 0 && e.key === 'ArrowLeft') return
          onClose()
        } else return
        e.preventDefault()
        e.stopPropagation()
      }}
    >
      {items.map((it, i) =>
        it.kind === 'separator' ? (
          <div key={`sep-${i}`} role="separator" className="clu-menu-sep" />
        ) : (
          <div
            key={it.id}
            ref={(el) => {
              rowRefs.current[i] = el
            }}
            role="menuitem"
            tabIndex={-1}
            aria-disabled={it.enabled === false}
            {...(it.submenu?.length ? { 'aria-haspopup': 'menu' as const, 'aria-expanded': openSub === i } : {})}
            data-item-id={it.id}
            className={`clu-menu-item ${it.destructive ? 'is-destructive' : ''}`}
            onPointerEnter={() => {
              if (it.enabled === false) return
              setFocus(i)
              setSubByKey(false)
              setOpenSub(it.submenu?.length ? i : null)
            }}
            onClick={() => activate(i)}
          >
            <span>{it.label}</span>
            {it.keys ? <span className="clu-menu-keys">{it.keys}</span> : null}
            {it.submenu?.length ? (
              <span className="clu-menu-chevron">
                <Chevron />
              </span>
            ) : null}
          </div>
        )
      )}
      {sub && sub.kind === 'item' && sub.submenu && subPos && depth < 2 ? (
        <MenuList
          items={sub.submenu}
          depth={depth + 1}
          label={sub.label}
          focusFirst={subByKey}
          closing={closing}
          onPick={onPick}
          onClose={() => {
            setOpenSub(null)
            rowRefs.current[focus]?.focus()
          }}
          style={{ position: 'fixed', left: subPos.left, top: subPos.top }}
        />
      ) : null}
    </div>
  )
}

export type { Item as MenuItemRow }
