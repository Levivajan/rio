// Sidebar composition. Usable from the UI and overlay bundles (hover peek).
import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties, type ReactNode } from 'react'
import { sameItems, useCluState, useWindowId } from '../../common/useCluState'
import { ArchiveButton } from '../archive/ArchiveButton'
import { ArchivePanel } from '../archive/ArchivePanel'
import { LibraryPeek } from '../archive/LibraryPeek'
import { Library } from '../archive/Library'
import { FileArrivals } from '../capabilities/FileArrivals'
import { titlebarPad, TopRow, useTitlebarShift } from '../shell/TopRow'
import { UrlPill } from '../shell/UrlPill'
import { UpdateBanner } from '../platform/UpdateBanner'
import { MediaCard } from '../split/MediaCard'
import { SpaceHeader } from '../spaces/SpaceHeader'
import { creatingSpace, NewSpaceCard, useNewSpaceCard } from '../spaces/NewSpaceCard'
import { SpaceSlider } from '../spaces/SpaceSlider'
import { SpaceSwitcher } from '../spaces/SpaceSwitcher'
import { useLibraryExit, useLibrarySwipe } from '../spaces/useLibrarySwipe'
import { FavoritesGrid } from '../tabs/FavoritesGrid'
import { SpaceTabs } from '../tabs/SpaceTabs'
import { useSidebarRequestBaseline } from '../tabs/sidebarRequest'
import { ZapBanner } from '../tweaks/ZapBanner'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'

export type SidebarMode = 'main' | 'peek' | 'private'

/** One space's pane: the favorites, the name row and the tab list slide together on a switch. The shown pane answers menu
 *  asks (`asks`), the docked sidebar's and the floating sidebar's each their own. */
function SpacePane({ spaceId, mode, asks = false }: { spaceId: string; mode: SidebarMode; asks?: boolean }) {
  return (
    <>
      {mode === 'private' ? null : <FavoritesGrid />}
      <SpaceHeader spaceId={spaceId} />
      <ArchivePanel>
        <SpaceTabs spaceId={spaceId} dragEnabled={mode !== 'peek'} answersAsks={asks} floating={mode === 'peek'} />
      </ArchivePanel>
    </>
  )
}

/** How long after it mounts the warm-up lays its panes out: after the window's first frames, before a first switch. */
export const WARM_DELAY_MS = 600

/**
 * Lays out the panes of `spaceIds` once, off screen and hidden, and drops them in the same task, so the
 * first time a pane really shows (the first floating sidebar, the first switch to a space by its dot) its styles,
 * fonts (an emoji space icon loads Apple's emoji font) and favicons are loaded and its first layout does not hold
 * the slide's frames. The copies never outlive the layout effect, so no second row is ever there to find.
 */
export function SidebarWarmup({ spaceIds, mode, extra }: { spaceIds: readonly string[]; mode: SidebarMode; extra?: ReactNode }) {
  const [phase, setPhase] = useState<'wait' | 'warm' | 'done'>('wait')
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (phase !== 'wait') return
    const t = setTimeout(() => setPhase('warm'), WARM_DELAY_MS)
    return () => clearTimeout(t)
  }, [phase])
  useLayoutEffect(() => {
    if (phase !== 'warm') return
    // Style and layout now, which loads the fonts and starts the favicons.
    ref.current?.getBoundingClientRect()
    setPhase('done')
  }, [phase])
  if (phase !== 'warm') return null
  return (
    <div ref={ref} className="clu-sidebar-warm" data-warm={spaceIds.length} aria-hidden="true" inert>
      {extra}
      {spaceIds.map((id) => (
        <div key={id} className="clu-sidebar-warm-pane">
          <SpacePane spaceId={id} mode={mode} />
        </div>
      ))}
    </div>
  )
}

/** The Library's box, open or sliding out after a swipe: the revealed full-screen bar moves it down by the bar's height
 *  as it does the rest of the column (the Library has no top padding of its own). */
export function librarySidebarStyle(width: number, reveal: number): CSSProperties {
  return { width, ...titlebarPad(reveal, '0px') }
}

export function Sidebar({ mode }: { mode: SidebarMode }) {
  const windowId = useWindowId() ?? ''
  const activeSpaceId = useCluState((s) => s.windows[windowId]?.activeSpaceId ?? '')
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  // The floating sidebar shows the Library, its pop-up and new files as the docked one does.
  const library = useCluState((s) => s.archivePanel[windowId] === true)
  // Before the early return: the tab list that answers menu asks is not there while the Library shows.
  useSidebarRequestBaseline(windowId)
  const card = useNewSpaceCard(activeSpaceId)
  // While the New Space panel is open the sidebar shows only its top row and a bottom bar with no Library button and no "+".
  const creating = useCluState((s) => mode === 'main' && creatingSpace(s, windowId))
  const others = useCluState((s) => s.spaceOrder.filter((id) => id !== activeSpaceId && s.spaces[id]), sameItems)
  const libraryRef = useRef<HTMLElement>(null)
  // A two-finger swipe left slides the Library back out to the first space.
  useLibrarySwipe(libraryRef, library && mode === 'main')
  // A swipe that closed it: the Library stays drawn left of the sidebar while both slide on.
  const navRef = useRef<HTMLElement>(null)
  const exit = useLibraryExit(navRef, library, width)
  // The revealed full-screen bar moves the whole column down (the Library has no top padding of its own).
  const reveal = useTitlebarShift()
  const pad = titlebarPad(reveal)
  // The Library takes the whole widened sidebar (the window buttons keep the top-left corner).
  // Its host keeps one key from the open Library to the exit slide, so the same Library (its section, its lists) slides
  // out with the sidebar instead of a fresh one mounting in the frame it closes (a one-frame swap
  // and a 683 ms paint stall).
  if (library)
    return (
      <nav ref={libraryRef} className="clu-sidebar clu-sidebar-library" aria-label="Sidebar" data-active-space={activeSpaceId} data-mode={mode} style={librarySidebarStyle(width, reveal)}>
        <div key="library" className="clu-library-host">
          <Library />
        </div>
      </nav>
    )
  return (
    <nav ref={navRef} className="clu-sidebar" aria-label="Sidebar" data-active-space={activeSpaceId} data-mode={mode} style={{ width, ...pad }}>
      {exit ? (
        <div key="library" className="clu-sidebar clu-sidebar-library clu-library-exit" aria-hidden="true" inert style={librarySidebarStyle(exit.width, reveal)}>
          <Library />
        </div>
      ) : null}
      <TopRow />
      {card || creating ? null : <UrlPill />}
      {card ? (
        <NewSpaceCard />
      ) : creating ? (
        // The overlay's New Space panel draws over this bare stretch of the sidebar.
        <div className="clu-sidebar-creating" />
      ) : (
        <SpaceSlider spaceId={activeSpaceId}>
          <SpacePane spaceId={activeSpaceId} mode={mode} asks />
        </SpaceSlider>
      )}
      {creating ? null : (
        <>
          <LibraryPeek />
          <FileArrivals />
          <MediaCard />
          <UpdateBanner />
        </>
      )}
      <footer className="clu-footer">
        {creating ? null : <ArchiveButton />}
        {mode === 'private' ? null : <SpaceSwitcher creating={creating} />}
      </footer>
      <ZapBanner />
      {mode === 'main' ? <SidebarWarmup spaceIds={others} mode={mode} /> : null}
    </nav>
  )
}
