// Space tab list: the Pinned tree with folders (the space header collapses it), then the Today divider
// with Clear, a New Tab row and Today, right-click row menu, rows sharing a splitId grouped, and the pointer drag of
// rows and folders (F8): the divider and the New Tab row are drop zones, an empty or collapsed Pinned shows a dashed
// zone while dragging.
// The empty area below the rows is no-drag: a right-click opens the empty-area menu and a double-click does nothing
// (design.md 3); a private window's + beside the divider opens the + menu (PLAN 8.2). Scrolling hides the hover card.
// The window's multi-selection drops rows that leave the sidebar order, and Esc clears it.
import { useEffect, useLayoutEffect, useMemo, useRef, type PointerEvent as ReactPointerEvent, type ReactNode } from 'react'
import type { NodeRef, TabState } from '../../../shared/state'
import { IconButton } from '../../common/IconButton'
import { PlusIcon } from '../../common/icons'
import { TreeRow } from '../../common/TreeRow'
import { sameItems, useCluState, useWindowId } from '../../common/useCluState'
import { SplitSidebarRow } from '../split/SplitSidebarRow'
import { FolderRow } from './FolderRow'
import { hoverLeave } from './hoverCard'
import { openMenuAt, openSpaceMenu, RENAME_SPACE_EVENT } from './renaming'
import { useSidebarRequest } from './sidebarRequest'
import { DragLayer } from './DragLayer'
import { actingTabs, dispatchSelection, selectableOrder } from './selection'
import { TabRow } from './TabRow'
import { TidyButton } from './TidyButton'
import { pressRow, useDrag, useDropFlip } from './useSidebarDrag'
import { useRowFlip } from './useRowFlip'
import { clu } from '../../common/clu'

function DraggableTabRow({ tab, active, dragEnabled, level }: { tab: TabState; active: boolean; dragEnabled: boolean; level: number }) {
  const drag = useMemo(() => ({ onPointerDown: (e: ReactPointerEvent<HTMLElement>) => dragEnabled && pressRow(e, tab.id, actingTabs(tab.id)) }), [tab.id, dragEnabled])
  return <TabRow tab={tab} active={active} level={level} drag={drag} />
}

/** Keyed by space: useCluState caches a selector's value per state object, so a list whose spaceId prop changes
 * without a new state object would keep the previous space's rows (docs/lanes/tabs/amendments.md). */
/** The header's border shows only while rows scroll under it, and the bottom bar's while rows run on under it. */
function markEdges(list: HTMLElement): void {
  const sidebar = list.closest('.clu-sidebar')
  sidebar?.toggleAttribute('data-scrolled', list.scrollTop > 0)
  sidebar?.toggleAttribute('data-rows-below', list.scrollTop + list.clientHeight < list.scrollHeight - 1)
}

export function SpaceTabs({ spaceId, dragEnabled = true, answersAsks = false, floating = false }: { spaceId: string; dragEnabled?: boolean; answersAsks?: boolean; floating?: boolean }) {
  return <SpaceTabList key={spaceId} spaceId={spaceId} dragEnabled={dragEnabled} answersAsks={answersAsks} floating={floating} />
}

function SpaceTabList({ spaceId, dragEnabled, answersAsks, floating }: { spaceId: string; dragEnabled: boolean; answersAsks: boolean; floating: boolean }) {
  const windowId = useWindowId() ?? ''
  const pinned = useCluState((s) => s.spaces[spaceId]?.pinned ?? NO_REFS)
  const pinnedCollapsed = useCluState((s) => s.spaces[spaceId]?.pinnedCollapsed ?? false)
  // While the command bar opens a new tab, New Tab takes the selected row style.
  const composing = useCluState((s) => s.windows[windowId]?.command?.mode === 'new-tab')
  const today = useCluState((s) => s.spaces[spaceId]?.today ?? [], sameItems)
  const folders = useCluState((s) => s.folders)
  const tabs = useCluState((s) => s.tabs)
  const activeTabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  const isPrivate = useCluState((s) => s.windows[windowId]?.private ?? false)
  const dragging = useDrag((v) => v.dragging)
  useSidebarRequest(windowId, answersAsks, floating)
  useDropFlip()
  // After the drop FLIP, so a dropped row keeps its slide from the ghost.
  const listRef = useRef<HTMLDivElement>(null)
  const folderShape = useCluState((s) => folderStates(s.folders), sameEntries)
  useRowFlip(listRef, { folders: folderShape, pinnedCollapsed })
  // Set from Clear's click until the state push that removes the rows; holds the fallback timer once the reply is in.
  const clearing = useRef<{ hold: ReturnType<typeof setTimeout> | null } | null>(null)
  const endClear = () => {
    listRef.current?.querySelector('.clu-tab-tree')?.removeAttribute('data-clearing')
    if (clearing.current?.hold) clearTimeout(clearing.current.hold)
    clearing.current = null
  }
  // After useRowFlip, which skips the faded rows' copies while the list is clearing.
  useLayoutEffect(() => {
    if (clearing.current) endClear()
  }, [today])
  // Marks whether rows run on under the bottom bar; it follows the list's size as well as its scroll. The bar draws no
  // border line over them.
  useEffect(() => {
    const list = listRef.current
    if (!list || typeof ResizeObserver === 'undefined') return
    const watch = new ResizeObserver(() => markEdges(list))
    watch.observe(list)
    if (list.firstElementChild) watch.observe(list.firstElementChild)
    return () => watch.disconnect()
  }, [])
  const order = useCluState((s) => selectableOrder(s, windowId), sameItems)
  useEffect(() => dispatchSelection({ type: 'prune', order }), [order])
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !e.defaultPrevented) dispatchSelection({ type: 'clear' })
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  const renderList = (ids: string[], level = 1) => {
    const out: ReactNode[] = []
    const seenSplits = new Set<string>()
    for (const id of ids) {
      const tab = tabs[id]
      if (!tab) continue
      if (tab.splitId) {
        if (seenSplits.has(tab.splitId)) continue
        seenSplits.add(tab.splitId)
        const members = ids.map((x) => tabs[x]).filter((t): t is TabState => !!t && t.splitId === tab.splitId)
        out.push(<SplitSidebarRow key={tab.splitId} tabs={members} activeTabId={activeTabId} level={level} />)
        continue
      }
      out.push(<DraggableTabRow key={id} tab={tab} active={id === activeTabId} dragEnabled={dragEnabled} level={level} />)
    }
    return out
  }

  /** The active tab, or its whole split (a split's members sit together in one list), for a collapsed folder. */
  const activeRun = (id: string): string[] => {
    const splitId = tabs[id]?.splitId
    return splitId ? Object.values(tabs).filter((t) => t.splitId === splitId).map((t) => t.id) : [id]
  }

  /** F3: a collapsed folder (or Pinned section) still shows the window's active tab beneath it. */
  const holdsActive = (refs: readonly NodeRef[], seen: Set<string>): boolean =>
    refs.some((r) => (r.kind === 'tab' ? r.id === activeTabId : !seen.has(r.id) && (seen.add(r.id), holdsActive(folders[r.id]?.children ?? [], seen))))

  const renderTree = (refs: readonly NodeRef[], level: number, seen: Set<string>): ReactNode[] => {
    const out: ReactNode[] = []
    let run: string[] = []
    const flush = () => {
      if (run.length) out.push(...renderList(run, level))
      run = []
    }
    for (const r of refs) {
      if (r.kind === 'tab') {
        run.push(r.id)
        continue
      }
      flush()
      const folder = folders[r.id]
      if (!folder || seen.has(r.id)) continue
      seen.add(r.id)
      out.push(<FolderRow key={r.id} folder={folder} level={level} dragEnabled={dragEnabled} />)
      if (!folder.collapsed) out.push(...renderTree(folder.children, level + 1, seen))
      else if (activeTabId && holdsActive(folder.children, new Set())) out.push(...renderList(activeRun(activeTabId), level + 1))
    }
    flush()
    return out
  }

  const showPinned = pinnedCollapsed ? (activeTabId && holdsActive(pinned, new Set()) ? renderList(activeRun(activeTabId)) : []) : renderTree(pinned, 1, new Set())

  const newTab = () => void clu().command.open({ mode: 'new-tab' })
  // Clear starts at once. In the click's first frame the titles go, the favicons and the
  // divider fade over about 50 ms (tabs.css, useRowFlip.ts) and New Tab slides up as soon as the push removes the rows.
  // The rows stay hidden until that push (endClear runs in its render's layout effect), so they never show again for a
  // frame between the reply and the push.
  const clearToday = () => {
    const tree = listRef.current?.querySelector('.clu-tab-tree')
    tree?.setAttribute('data-clearing', '')
    const run = { hold: null as ReturnType<typeof setTimeout> | null }
    clearing.current = run
    clu()
      .archive.clearToday({ spaceId })
      .then(
        () => {
          if (clearing.current === run) run.hold = setTimeout(endClear, CLEAR_HOLD_MS)
        },
        () => {
          if (clearing.current === run) endClear()
        }
      )
  }
  return (
    <div
      ref={listRef}
      className="clu-tab-list"
      onScroll={(e) => {
        hoverLeave()
        markEdges(e.currentTarget)
      }}
      onContextMenu={(e) => {
        if (e.target !== e.currentTarget) return
        e.preventDefault()
        // Empty sidebar space opens the space's menu at the pointer.
        void openSpaceMenu(spaceId, { x: Math.round(e.clientX), y: Math.round(e.clientY) }, () => window.dispatchEvent(new CustomEvent(RENAME_SPACE_EVENT, { detail: spaceId })))
      }}
    >
      <div className="clu-tab-tree" role="tree" aria-label="Tabs">
        {/* Neither section has a label: the pinned rows sit right under the space header (which collapses
            them) and a divider with Clear sets Today apart. While dragging, an empty or collapsed Pinned gets a zero-height
            drop marker, so nothing moves. */}
        {dragging && (!pinned.length || pinnedCollapsed) ? <div className="clu-drop-marker" data-drop-zone="pinned" aria-hidden="true" /> : null}
        {showPinned}
        <div className="clu-today-divider" data-section="today" data-drop-zone="today" data-clearable={today.length > 0} />
        <TreeRow selected={false} className="clu-new-tab" data-new-tab="true" data-active={composing || undefined} data-drop-zone="today" onClick={newTab} onKeyDown={(e) => e.key === 'Enter' && newTab()}>
          <PlusIcon />
          <span className="clu-row-title">New Tab</span>
        </TreeRow>
        {renderList(today)}
      </div>
      {/* Outside the tree (a tree owns only tree items), anchored to the Today divider in CSS: Clear shows while the
          pointer is over the sidebar. A normal window's + is the space switcher's in the footer; a private window has no
          switcher, so its + sits here. */}
      {today.length ? (
        <button type="button" className="clu-today-clear" data-clear-today="true" onClick={clearToday}>
          {/* A down arrow before the label (12 x 9 box at w - 58.5). */}
          <svg width="9" height="9" viewBox="0 0 9 9" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
            <path d="M4.5 1v7M1.5 5l3 3 3-3" />
          </svg>
          Clear
        </button>
      ) : null}
      <TidyButton spaceId={spaceId} count={today.length} />
      {isPrivate ? (
        <IconButton
          aria-label="New"
          className="clu-section-new"
          data-new-menu="true"
          onClick={(e) => {
            const r = e.currentTarget.getBoundingClientRect()
            void openMenuAt('new', { x: r.x, y: r.y, width: r.width, height: r.height })
          }}
        >
          <PlusIcon />
        </IconButton>
      ) : null}
      <DragLayer />
    </div>
  )
}

const NO_REFS: NodeRef[] = []
/** How long faded rows wait for the state push after the reply before they show again (nothing was cleared). */
const CLEAR_HOLD_MS = 1000

/** Folder id to collapsed, for the row motion's timing. */
function folderStates(folders: Readonly<Record<string, { collapsed: boolean }>>): Record<string, boolean> {
  const out: Record<string, boolean> = {}
  for (const [id, f] of Object.entries(folders)) out[id] = f.collapsed
  return out
}

function sameEntries(a: Record<string, boolean>, b: Record<string, boolean>): boolean {
  const ka = Object.keys(a)
  return ka.length === Object.keys(b).length && ka.every((k) => a[k] === b[k])
}
