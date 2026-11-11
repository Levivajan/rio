// The Library's Spaces section: every space as a tall card tinted with its own theme, side by side in a row
// that scrolls sideways. The header carries the space icon (a dashed square on hover, which changes the icon), the
// name (a click switches to the space, a double-click renames), the profile pill and a brush button for the theme; the
// space's pinned rows follow (a click opens the tab in its space); the foot has a move handle (drag it onto another
// card to reorder) and "…" (the space's menu). A round "+" after the last card adds a space.
// While it shows, the Library widens until every card and the "+" fit (archive.setWidth; the
// browser still keeps the page's minimum room), and goes back to its 417 when another section shows.
import { useEffect, useLayoutEffect, useRef, useState, type DragEvent, type SVGProps } from 'react'
import { LetterTile } from '../../common/LetterTile'
import { hasGlyph, SpaceIconGlyph } from '../../common/spaceSymbols'
import { clu } from '../../common/clu'
import { menuLight } from '../../common/menuChrome'
import { sameItems, useCluState, useWindowId } from '../../common/useCluState'
import type { NodeRef, SpaceState } from '../../../shared/state'
import { defaultThemeFor, themeToCss } from '../../../shared/theme/space'
import type { ResolvedMode } from '../../../shared/theme/appearance'
import { RenameField } from '../spaces/SpaceHeader'
import { RowIcon } from '../tabs/RowIcon'
import { rowView } from '../tabs/rowModel'
import { openSpaceMenu } from '../tabs/renaming'

const DRAG_TYPE = 'application/x-clu-library-space'
const line = { fill: 'none', stroke: 'currentColor', strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const SmileyIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={12} height={12} viewBox="0 0 12 12" {...line} strokeWidth={1.2} {...p}>
    <circle cx="6" cy="6" r="5" />
    <path d="M4.5 4.25v1M7.5 4.25v1M3.9 7.4a2.6 2.6 0 0 0 4.2 0" />
  </svg>
)
const BrushIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={14} height={14} viewBox="0 0 14 14" fill="currentColor" aria-hidden="true" {...p}>
    <path d="M12.6 1.4a.9.9 0 0 0-1.27 0L6.2 6.53l1.27 1.27 5.13-5.13a.9.9 0 0 0 0-1.27Z" />
    <path d="M5.55 7.3a2 2 0 0 0-2.4.33c-.7.7-.45 1.7-.95 2.45-.3.45-.8.6-1.2.62.65.7 1.7.95 2.75.75a3.2 3.2 0 0 0 2.05-1.3 1.9 1.9 0 0 0 .3-1.6Z" />
  </svg>
)
const MoveIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={14} height={14} viewBox="0 0 14 14" {...line} strokeWidth={1.3} {...p}>
    <path d="M7 1v12M1 7h12M5.25 2.75 7 1l1.75 1.75M5.25 11.25 7 13l1.75-1.75M2.75 5.25 1 7l1.75 1.75M11.25 5.25 13 7l-1.75 1.75" />
  </svg>
)
const MoreIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={16} height={16} viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" {...p}>
    <circle cx="2.95" cy="8" r="1.25" /><circle cx="8" cy="8" r="1.25" /><circle cx="13.05" cy="8" r="1.25" />
  </svg>
)
const ClosedFolderIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg width={16} height={16} viewBox="0 0 16 16" {...line} strokeWidth={1.4} {...p}>
    <path d="M1.75 12.5V3.5a1 1 0 0 1 1-1h3.5l1.5 1.75h5.5a1 1 0 0 1 1 1v7.25a1 1 0 0 1-1 1H2.75a1 1 0 0 1-1-1Z" />
    <path d="M1.75 6.25h12.5" />
  </svg>
)

const rectOf = (el: Element) => {
  const r = el.getBoundingClientRect()
  return { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height) }
}

/** The card's paint: the space's own theme (the default gradient of its color when it has none) in the current mode. */
export function cardPaint(space: Pick<SpaceState, 'theme' | 'color'>, mode: ResolvedMode) {
  const css = themeToCss(space.theme ?? defaultThemeFor(space.color), mode, !space.theme)
  return { background: css.background, color: css.ink, '--card-muted': css.inkMuted }
}

function PinnedRow({ node, spaceId }: { node: NodeRef; spaceId: string }) {
  const tab = useCluState((s) => (node.kind === 'tab' ? s.tabs[node.id] : undefined))
  const folder = useCluState((s) => (node.kind === 'folder' ? s.folders[node.id] : undefined))
  if (node.kind === 'folder') {
    if (!folder) return null
    return (
      <li className="clu-space-card-row" data-folder>
        <button type="button" onClick={() => switchTo(spaceId)}>
          <span className="clu-space-card-row-icon">{folder.icon ? <span aria-hidden="true">{folder.icon}</span> : <ClosedFolderIcon />}</span>
          <span className="clu-space-card-row-title">{folder.name || 'Untitled Folder'}</span>
        </button>
      </li>
    )
  }
  if (!tab) return null
  const { title } = rowView(tab)
  return (
    <li className="clu-space-card-row">
      <button
        type="button"
        title={title}
        onClick={() => {
          void clu().spaces.switch({ spaceId })
          void clu().tabs.activate({ tabId: tab.id })
          void clu().archive.close()
        }}
      >
        <span className="clu-space-card-row-icon"><RowIcon tab={tab} title={title} size={16} /></span>
        <span className="clu-space-card-row-title">{title}</span>
      </button>
    </li>
  )
}

function switchTo(spaceId: string) {
  void clu().spaces.switch({ spaceId })
  void clu().archive.close()
}

function SpaceCard({ space, index, mode }: { space: SpaceState; index: number; mode: ResolvedMode }) {
  const profile = useCluState((s) => s.profiles[space.profileId]?.name ?? '')
  const [renaming, setRenaming] = useState(false)
  const [over, setOver] = useState(false)
  const name = space.name || 'Untitled Space'
  const accepts = (e: DragEvent) => e.dataTransfer.types.includes(DRAG_TYPE)
  return (
    <li
      className="clu-space-card"
      data-space-id={space.id}
      data-drop={over || undefined}
      style={cardPaint(space, mode)}
      onDragOver={(e) => {
        if (!accepts(e)) return
        e.preventDefault()
        e.dataTransfer.dropEffect = 'move'
        setOver(true)
      }}
      onDragLeave={() => setOver(false)}
      onDrop={(e) => {
        setOver(false)
        const from = e.dataTransfer.getData(DRAG_TYPE)
        if (!accepts(e) || !from || from === space.id) return
        e.preventDefault()
        void clu().spaces.reorder({ spaceId: from, toIndex: index })
      }}
    >
      <div className="clu-space-card-head">
        <button
          type="button"
          className="clu-space-card-icon"
          aria-label="Change Space Icon"
          onClick={(e) => void clu().spaces.openIconMenu({ spaceId: space.id, anchor: rectOf(e.currentTarget), light: menuLight() })}
        >
          {hasGlyph(space.icon) ? <SpaceIconGlyph icon={space.icon} size={14} /> : <LetterTile text={name} size={16} />}
        </button>
        <span className="clu-space-card-name">
          {renaming ? (
            <RenameField spaceId={space.id} name={space.name} onDone={() => setRenaming(false)} />
          ) : (
            <button type="button" title={name} onClick={() => switchTo(space.id)} onDoubleClick={() => space.name && setRenaming(true)}>
              {name}
            </button>
          )}
        </span>
        {profile ? (
          <span className="clu-space-card-profile" title={`Profile: ${profile}`}>
            <SmileyIcon />
            <span>{profile}</span>
          </span>
        ) : null}
        <button
          type="button"
          className="clu-space-card-brush"
          aria-label="Edit Theme Color"
          onClick={(e) => void clu().theme.openPicker({ spaceId: space.id, anchor: rectOf(e.currentTarget) })}
        >
          <BrushIcon />
        </button>
      </div>
      <ul className="clu-space-card-rows" aria-label={`${name} pinned tabs`}>
        {space.pinned.map((node) => (
          <PinnedRow key={`${node.kind}:${node.id}`} node={node} spaceId={space.id} />
        ))}
      </ul>
      <div className="clu-space-card-foot">
        <span
          className="clu-space-card-move"
          role="button"
          tabIndex={-1}
          aria-label="Move Space"
          draggable
          onDragStart={(e) => {
            e.dataTransfer.setData(DRAG_TYPE, space.id)
            e.dataTransfer.effectAllowed = 'move'
            const card = e.currentTarget.closest('.clu-space-card')
            if (card) e.dataTransfer.setDragImage(card, 24, card.clientHeight - 16)
          }}
        >
          <MoveIcon />
        </span>
        <button
          type="button"
          className="clu-space-card-more"
          aria-label="Space options"
          aria-haspopup="menu"
          onClick={(e) => {
            const r = e.currentTarget.getBoundingClientRect()
            void openSpaceMenu(space.id, { x: Math.round(r.x), y: Math.round(r.y + r.height) }, () => space.name && setRenaming(true))
          }}
        >
          <MoreIcon />
        </button>
      </div>
    </li>
  )
}

/** The sidebar width at which the card row shows whole: the frame's room outside the row (laid-out widths, so a
 *  slide's transform never skews it), the row's padding, its items and the gaps between them. */
export function spacesLibraryWidth(m: { frame: number; row: number; padding: number; gap: number; items: number[] }): number {
  const fin = (n: number) => (Number.isFinite(n) ? Math.max(0, n) : 0)
  const outside = fin(fin(m.frame) - fin(m.row))
  const items = m.items.reduce((sum, w) => sum + fin(w), 0) + fin(m.gap) * Math.max(0, m.items.length - 1)
  return Math.ceil(outside + fin(m.padding) + items)
}

function measureRow(row: HTMLElement): number | null {
  const frame = row.closest<HTMLElement>('.clu-sidebar-frame')
  if (!frame) return null
  const css = getComputedStyle(row)
  const px = (v: string) => parseFloat(v) || 0
  return spacesLibraryWidth({
    frame: frame.offsetWidth,
    row: row.clientWidth,
    padding: px(css.paddingLeft) + px(css.paddingRight),
    gap: px(css.columnGap),
    items: Array.from(row.children, (c) => (c as HTMLElement).offsetWidth)
  })
}

/** The Spaces view whose width the browser holds now. */
let asker: object | null = null

export function SpacesView() {
  const order = useCluState((s) => s.spaceOrder, sameItems)
  const spaces = useCluState((s) => s.spaces)
  const mode = useCluState((s) => s.app.appearance.resolved)
  const windowId = useWindowId() ?? ''
  // A private window's unsaved space is not a space to manage.
  const cards = order.map((id) => spaces[id]).filter((sp): sp is SpaceState => Boolean(sp) && !sp?.ephemeral)
  const open = useCluState((s) => s.archivePanel[windowId] === true)
  const shown = useCluState((s) => s.windows[windowId]?.shell?.layout?.sidebarRect?.width ?? 0)
  const row = useRef<HTMLUListElement>(null)
  // Asks again whenever the row or a card resizes, the Library opens or the browser's width changes: the browser
  // forgets the width on close, a close and reopen can land in one state patch, and the same view can stay mounted
  // from the close slide into a quick reopen. The browser ignores a width it already holds.
  const self = useRef({})
  useLayoutEffect(() => {
    const el = row.current
    if (!el || !open) return
    let sent = 0
    const ask = () => {
      const width = measureRow(el)
      if (!width || width === sent) return
      sent = width
      asker = self.current
      void clu().archive.setWidth({ width: Math.min(width, 4096) })
    }
    ask()
    if (typeof ResizeObserver !== 'function') return
    const ro = new ResizeObserver(ask)
    ro.observe(el)
    for (const c of Array.from(el.children)) ro.observe(c)
    return () => ro.disconnect()
  }, [cards.length, open, shown])
  // Only the view that asked last gives the width back: the Library sliding out with the sidebar unmounts after a quick
  // reopen has asked.
  useEffect(
    () => () => {
      if (asker !== self.current) return
      asker = null
      void clu().archive.setWidth({ width: 0 })
    },
    []
  )
  return (
    <section className="clu-archive clu-library-spaces" aria-label="Spaces">
      <ul ref={row} className="clu-space-cards">
        {cards.map((sp, i) => (
          <SpaceCard key={sp.id} space={sp} index={i} mode={mode} />
        ))}
        <li className="clu-space-cards-add">
          <button type="button" aria-label="New Space" onClick={() => void clu().commands.run({ id: 'spaces.newSpace' })}>
            <svg width={14} height={14} viewBox="0 0 14 14" {...line} strokeWidth={1.6}><path d="M7 2v10M2 7h10" /></svg>
          </button>
        </li>
      </ul>
    </section>
  )
}
