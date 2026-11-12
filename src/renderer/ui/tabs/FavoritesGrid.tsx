// Favorites grid (02-tabs.md F1): icon tiles for the profile of the window's space, shared by every space of
// that profile. The width sets the columns (2 below 173, 3 from 173, 4 from 228; tiles 47 tall
// and as wide as the columns make them); tiles stretch to fill the row when there
// are fewer; the 16 pt favicon sits alone in the tile with no loaded state. A hover shows no title
// tooltip and a tile carries no speaker badge (audio shows only on rows and the media bar). The active
// tile takes the selected row's fill (it follows the theme) and a ring in the space's accent, the theme's main color
// (a gray site icon gave a gray ring), and a page whose title carries an unread count gets a 5 pt red dot on the favicon's
// top right corner.
// Tiles drag and select like rows (F8, multi-select) and select on the press, as rows do;
// with zero favorites a drag gets a zero-height drop marker, so nothing moves. A mail favorite opens its preview
// card on the sidebar just under the tile, 230 ms into a hover; every other preview site (previewSite) opens its card
// beside the sidebar, level with the tile;
// other tiles show nothing on hover.
import { memo } from 'react'
import type { TabState } from '../../../shared/state'
import { sameItems, useCluState, useWindowId } from '../../common/useCluState'
import { hoverEnter, hoverLeave, hoverPointerLeave } from './hoverCard'
import { RowIcon } from './RowIcon'
import { openRowMenu } from './renaming'
import { rowView } from './rowModel'
import { actingTabs, contextMenuFor, selectOnClick, useSelected } from './selection'
import { pressRow, useDrag, useDropFlip } from './useSidebarDrag'
import './tabs.css'
import { clu } from '../../common/clu'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'
import { favoriteColumns } from '../../../shared/shell/sidebarMetrics'
import { HOVER_CARD, LIVE_PREVIEW, mailCardAnchor, MAIL_PREVIEW, previewSite } from '../../../shared/tabs'

// Memoized: a patch to one favorite's tab re-renders that tile only.
const FavoriteTile = memo(function FavoriteTile({ tab, active }: { tab: TabState; active: boolean }) {
  const v = rowView(tab)
  const preview = previewSite(tab.url) !== null
  const mail = previewSite(tab.url) === 'Gmail'
  const loaded = tab.lifecycle === 'live'
  const selected = useSelected(tab.id)
  return (
    <div
      role="listitem"
      className="clu-favorite"
      data-tab-id={tab.id}
      data-active={String(active)}
      data-loaded={String(loaded)}
      data-loading={String(tab.loading)}
      data-audible={String(tab.audible)}
      data-muted={String(tab.muted)}
      data-off-pinned={String(v.offPinned)}
      data-selected={String(selected)}
      data-unread={String(v.unread && tab.unreadBadge)}
      onPointerEnter={
        preview
          ? (e) => {
              // The card opens beside the sidebar as a pinned row's does, 12.5 right of the grid's edge (the
              // rows' edge), its top level with the tile; never over the sidebar (hoverCardRect adds the offsets).
              // Gmail's card hangs from the tile's lower right corner, 3.5 over it
              // (the first tile's card at left 65, top 133.5); a tile in a later
              // column hangs it at the first column's place, so it starts inside the sidebar (mailCardAnchor).
              const r = e.currentTarget.getBoundingClientRect()
              const grid = e.currentTarget.parentElement ?? e.currentTarget
              const edge = grid.getBoundingClientRect().right
              const first = grid.querySelector('.clu-favorite') ?? e.currentTarget
              const at = mail
                ? mailCardAnchor(r, first.getBoundingClientRect().right)
                : { x: edge + LIVE_PREVIEW.rowGap - HOVER_CARD.offsetX, y: r.top - HOVER_CARD.offsetY }
              hoverEnter(tab.id, e.currentTarget, at, MAIL_PREVIEW.delayMs, mail)
            }
          : undefined
      }
      onPointerLeave={preview ? hoverPointerLeave : undefined}
      onPointerDown={(e) => {
        if (preview) hoverLeave()
        if (e.button === 0 && !e.metaKey && !e.shiftKey && !e.ctrlKey && !active) void clu().tabs.activate({ tabId: tab.id })
        pressRow(e, tab.id, actingTabs(tab.id))
      }}
    >
      <button
        type="button"
        className="clu-favorite-tile"
        aria-label={v.title}
        aria-current={active ? 'page' : undefined}
        // The press already selected the tile; a click only changes the selection, or activates from the keyboard.
        onClick={(e) => selectOnClick(e, tab.id) || (e.detail === 0 && void clu().tabs.activate({ tabId: tab.id }))}
        onContextMenu={(e) => {
          e.preventDefault()
          void openRowMenu(tab.id, e.clientX, e.clientY, contextMenuFor(tab.id))
        }}
      >
        <span className="clu-favorite-icon">
          <RowIcon tab={tab} title={v.title} />
        </span>
        {v.unread && tab.unreadBadge ? <span className="clu-favorite-unread" aria-hidden="true" /> : null}
      </button>
    </div>
  )
})

export function FavoritesGrid() {
  const windowId = useWindowId() ?? ''
  const ids = useCluState((s) => {
    const spaceId = s.windows[windowId]?.activeSpaceId
    const profileId = spaceId ? s.spaces[spaceId]?.profileId : undefined
    return (profileId ? s.favorites[profileId] : undefined) ?? []
  }, sameItems)
  const shown = useCluState((s) => ids.map((id) => s.tabs[id]).filter((t): t is TabState => !!t), sameItems)
  const activeTabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  const columns = Math.max(1, Math.min(favoriteColumns(width), shown.length))
  const dragging = useDrag((v) => v.dragging)
  useDropFlip()
  if (shown.length === 0)
    return dragging ? <div className="clu-drop-marker" data-drop-zone="favorites" aria-hidden="true" /> : null
  return (
    <div className="clu-favorites" role="list" aria-label="Favorites" data-count={shown.length} data-columns={columns} style={{ gridTemplateColumns: `repeat(${columns}, minmax(0, 1fr))` }}>
      {shown.map((t) => (
        <FavoriteTile key={t.id} tab={t} active={t.id === activeTabId} />
      ))}
    </div>
  )
}
