// The sidebar's anchored menus (docs/chromium/parity-sidebar.md): the narrow top row's "…" and
// the bottom bar's "+" open a dark popover with an arrow toward their button, not a native menu. The
// visible panel is 200 wide for "…" and 210
// for "+", rows are 40 tall with a 21 pt icon 17.5 and the label 50 from the panel's edge and the shortcut 16 from its
// right, the hovered row is the space's accent inset 7.5, separators take 16. The browser builds the items
// (CluTabMenu) and runs the pick.
import { useEffect, useMemo, useRef, useState, type ReactElement } from 'react'
import { ReloadIcon } from '../../common/icons'
import { shortcutKeys } from '../../common/Tooltip'
import { useCluState } from '../../common/useCluState'
import { SHORTCUTS_KEY } from '../../../shared/shortcuts'
import type { Rect } from '../../../shared/types'
import { closeInstance, respond, type SurfaceProps } from '../core/types'
import './menuPopover.css'

export interface PopoverItem { id: string; label: string; enabled: boolean; command?: string }
export type PopoverEntry = PopoverItem | { separator: true }
export interface PopoverProps { menu: 'more' | 'new'; items: PopoverEntry[] }

export const POPOVER = {
  more: { width: 200, placement: 'below' as const },
  new: { width: 210, placement: 'above' as const },
  row: 40,
  padding: 8,
  separator: 16,
  arrow: { width: 16, height: 7 },
  gap: 5.5,
  // The "…" panel's top sits 4 lower than the "+" panel's gap gives (y 54).
  moreDrop: 4,
  margin: 6
} as const

const svg = { width: 21, height: 21, viewBox: '0 0 18 18', fill: 'none', stroke: 'currentColor', strokeWidth: 1.5, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const

/** Rio's own line icons for the items, by the menu item id the browser sends. */
const ICONS: Record<string, () => ReactElement> = {
  'nav.reload': () => <ReloadIcon width={21} height={21} />,
  'sidebar.toggle': () => (
    <svg {...svg}><rect x="2" y="3.5" width="14" height="11" rx="2.2" /><path d="M7 3.5v11" /></svg>
  ),
  'spaces.newSpace': () => (
    <svg {...svg}><rect x="5" y="5" width="10.5" height="10.5" rx="2" /><path d="M3 12.5v-8A1.5 1.5 0 0 1 4.5 3h8M10.25 7.75v5M7.75 10.25h5" /></svg>
  ),
  'folders.new': () => (
    <svg {...svg}><path d="M2.5 5.5A1.5 1.5 0 0 1 4 4h3.2l1.6 1.8H14a1.5 1.5 0 0 1 1.5 1.5v5.7A1.5 1.5 0 0 1 14 14.5H4A1.5 1.5 0 0 1 2.5 13Z" /><path d="M2.5 8h13" /></svg>
  ),
  'split.add': () => (
    <svg {...svg}><rect x="2.5" y="3.5" width="13" height="11" rx="2" /><path d="M9 3.5v11M12.25 7.5v3M10.75 9h3" /></svg>
  ),
  'tabs.newTab': () => (
    <svg {...svg}><rect x="2.5" y="2.5" width="13" height="13" rx="2.5" /><path d="M9 6v6M6 9h6" /></svg>
  )
}

const isItem = (e: PopoverEntry): e is PopoverItem => !('separator' in e)

/** The panel's left and top and the arrow's x inside it: centered under (or over) the button, kept in the window. */
export function placePopover(anchor: Rect, menu: 'more' | 'new', height: number, viewport: { width: number; height: number }) {
  const { width, placement } = POPOVER[menu]
  const center = anchor.x + anchor.width / 2
  // "…": the panel is centered on the button; "+": its arrow sits 27.5 from the right end.
  const wanted = menu === 'more' ? center - width / 2 : center - width + 27.5
  const left = Math.round(Math.min(Math.max(POPOVER.margin, wanted), Math.max(POPOVER.margin, viewport.width - width - POPOVER.margin)))
  const top =
    placement === 'below'
      ? Math.round(anchor.y + anchor.height + POPOVER.gap + POPOVER.arrow.height + (menu === 'more' ? POPOVER.moreDrop : 0))
      : Math.round(anchor.y - POPOVER.gap - POPOVER.arrow.height - height)
  const arrowX = Math.round(Math.min(Math.max(14, center - left), width - 14))
  return { left, top: Math.max(POPOVER.margin, top), arrowX, placement }
}

export function popoverHeight(items: readonly PopoverEntry[]): number {
  return POPOVER.padding * 2 + items.reduce((h, e) => h + (isItem(e) ? POPOVER.row : POPOVER.separator), 0)
}

export function MenuPopover({ instance }: SurfaceProps) {
  const props = (instance.props ?? {}) as Partial<PopoverProps>
  const menu = props.menu === 'new' ? 'new' : 'more'
  const items = useMemo(() => (Array.isArray(props.items) ? props.items : []), [props.items])
  const anchor = instance.rect ?? { x: 0, y: 0, width: 0, height: 0 }
  const custom = useCluState((s) => s.settings.global[SHORTCUTS_KEY])
  const enabled = items.filter(isItem).filter((i) => i.enabled)
  const [active, setActive] = useState<string | null>(null)
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    ref.current?.focus()
  }, [])

  const pick = (id: string) => respond(instance.instanceId, id)
  const onKey = (e: React.KeyboardEvent) => {
    if (e.key === 'Escape') {
      e.preventDefault()
      closeInstance(instance.instanceId)
      return
    }
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault()
      const at = enabled.findIndex((i) => i.id === active)
      const step = e.key === 'ArrowDown' ? 1 : -1
      const next = enabled[(at + step + enabled.length + (at < 0 && step < 0 ? 1 : 0)) % enabled.length]
      setActive(next?.id ?? null)
      return
    }
    if ((e.key === 'Enter' || e.key === ' ') && active) {
      e.preventDefault()
      pick(active)
    }
  }

  const height = popoverHeight(items)
  const pos = placePopover(anchor, menu, height, { width: window.innerWidth, height: window.innerHeight })
  return (
    <div
      ref={ref}
      role="menu"
      tabIndex={-1}
      aria-label={menu === 'new' ? 'New' : 'More options'}
      className="clu-menu-popover"
      data-menu={menu}
      data-placement={pos.placement}
      style={{ left: pos.left, top: pos.top, width: POPOVER[menu].width, height }}
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={onKey}
    >
      <span className="clu-menu-popover-arrow" aria-hidden="true" style={{ left: pos.arrowX - POPOVER.arrow.width / 2 }} />
      {items.map((entry, i) => {
        if (!isItem(entry)) return <div key={`sep-${i}`} role="separator" className="clu-menu-popover-sep" />
        const keys = shortcutKeys(entry.command, custom)
        const Icon = ICONS[entry.id]
        return (
          <button
            key={entry.id}
            type="button"
            role="menuitem"
            className="clu-menu-popover-row"
            data-id={entry.id}
            data-active={active === entry.id || undefined}
            disabled={!entry.enabled}
            onPointerEnter={() => setActive(entry.enabled ? entry.id : null)}
            onPointerLeave={() => setActive((a) => (a === entry.id ? null : a))}
            onClick={() => pick(entry.id)}
          >
            <span className="clu-menu-popover-icon">{Icon ? <Icon /> : null}</span>
            <span className="clu-menu-popover-label">{entry.label}</span>
            {keys.length ? <span className="clu-menu-popover-keys" aria-label={keys.join('')}>{keys.join(' ')}</span> : null}
          </button>
        )
      })}
    </div>
  )
}
