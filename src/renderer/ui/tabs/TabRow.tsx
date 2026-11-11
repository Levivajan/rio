// One tab row in Pinned, a folder or Today (02-tabs.md F2, F9, F11, design.md section 2). A TreeRow with
// the row's data attributes; the favicon of an off-pinned row returns it to its pinned URL; the speaker toggles mute
// without activating the row; close and mute controls appear on hover and reserve no width at rest. Resting on the row
// shows its hover card (F15). Cmd+click and Shift+click select rows (data-selected) instead of activating them, and a
// right-click on a row of a multi-selection opens the multi-select menu. A press selects the row at once, before any
// drag, and a second click on the title of the pinned row already selected renames it inline.
import { useEffect, useReducer, useRef, type PointerEvent as ReactPointerEvent } from 'react'
import type { TabState } from '../../../shared/state'
import { CloseIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import { TreeRow } from '../../common/TreeRow'
import { indent } from './FolderRow'
import { hoverEnter, hoverLeave, hoverMove, hoverPointerLeave } from './hoverCard'
import { RenameField } from './RenameField'
import { openRowMenu, setRenaming, useRenaming } from './renaming'
import { RowIcon } from './RowIcon'
import { rowView } from './rowModel'
import { useSpeakerSoon } from './useSpeakerSoon'
import { MorphTitle } from './MorphTitle'
import { observeTidy, TIDY_MORPH_MS, tidiedJustNow, tidyExpiresIn } from './tidied'
import { contextMenuFor, selectOnClick, useSelected } from './selection'
import { MinusIcon, SpeakerIcon, SpeakerMutedIcon } from './tabIcons'
import './tabs.css'
import { clu } from '../../common/clu'
import { HOVER_CARD, hoverCardModel, isMailPreview, LIVE_PREVIEW, MAIL_PREVIEW, previewSite } from '../../../shared/tabs'
import { useCluState, useWindowId } from '../../common/useCluState'

export interface RowDragHandlers {
  onPointerDown(e: ReactPointerEvent<HTMLElement>): void
}

/** Controls inside a row never start a drag. */
const keepPointer = (e: ReactPointerEvent) => e.stopPropagation()

export function TabRow({ tab, active, level = 1, drag }: { tab: TabState; active: boolean; level?: number; drag: RowDragHandlers }) {
  const v = rowView(tab)
  const preview = tab.section !== 'today' && previewSite(tab.url) !== null
  const audio = useSpeakerSoon(v.audio)
  const tidy = observeTidy(tab, active)
  // The hint expires on a clock too, so the row renders once more when it does.
  const [, rerender] = useReducer((n: number) => n + 1, 0)
  // Once the title's morph has played the row renders the plain title again (it keeps the ellipsis).
  const morphing = tidy && tidiedJustNow(tab.id)
  useEffect(() => {
    if (!morphing) return
    const t = setTimeout(rerender, TIDY_MORPH_MS + 1)
    return () => clearTimeout(t)
  }, [morphing])
  useEffect(() => {
    const ms = tidy ? tidyExpiresIn(tab.id) : null
    if (ms === null) return
    const t = setTimeout(rerender, ms + 1)
    return () => clearTimeout(t)
  }, [tidy, tab.id])
  const activate = () => void clu().tabs.activate({ tabId: tab.id })
  const activeAtPress = useRef(false)
  const renaming = useRenaming(tab.id)
  const selected = useSelected(tab.id)
  // The right-clicked row keeps its hover fill while its menu is open.
  const windowId = useWindowId() ?? ''
  // The row that opened a Peek (the active one) shows the peeked page's favicon at its right end.
  const peekIcon = useCluState((s) => (active ? (s.windows[windowId]?.peek?.favicon ?? null) : null))
  const menuOpen = useCluState((s) => (s.windows[windowId]?.overlay.surfaces ?? []).some((o) => o.surface === 'menu' && (o.props as { tabId?: unknown } | null)?.tabId === tab.id))
  // F3: pinned rows rename on double-click, Today rows only from the menu. An empty value clears the override, so the
  // row shows the page title again.
  const rename = (next: string | null) => {
    setRenaming(null)
    if (next !== null && next !== v.title) void clu().tabs.rename({ nodeId: tab.id, title: next })
  }
  return (
    <TreeRow
      selected={active}
      level={level}
      style={indent(level)}
      className="clu-tab-row"
      data-tab-id={tab.id}
      data-active={String(active)}
      data-loaded={String(tab.lifecycle === 'live')}
      data-loading={String(tab.loading)}
      data-audible={String(tab.audible)}
      data-muted={String(tab.muted)}
      data-off-pinned={String(v.offPinned)}
      data-tidied={tidy || undefined}
      data-renaming={String(renaming)}
      data-selected={String(selected)}
      data-menu-open={menuOpen || undefined}
      onClick={(e) => {
        if (selectOnClick(e, tab.id)) return
        if (activeAtPress.current && e.detail === 1 && tab.section === 'pinned' && e.target instanceof Element && e.target.closest('.clu-row-title')) {
          setRenaming(tab.id)
          return
        }
        if (!active) activate()
      }}
      onDoubleClick={() => tab.section === 'pinned' && setRenaming(tab.id)}
      onContextMenu={(e) => {
        e.preventDefault()
        hoverLeave()
        void openRowMenu(tab.id, e.clientX, e.clientY, contextMenuFor(tab.id))
      }}
      onKeyDown={(e) => {
        if (e.key === 'Enter') activate()
      }}
      onPointerEnter={(e) => {
        // A pinned row on a preview site hangs its card 12.5 right of the row, centered on it, as a
        // favorite's does 230 ms in; the frame sends the anchor hoverCardRect turns into that place. The card is the
        // one the browser will draw, the 256 tall live view for a loaded or an asleep row alike.
        // Gmail's card grows with the unread messages the browser reads, so it hangs level with the row's top.
        if (preview) {
          const r = e.currentTarget.getBoundingClientRect()
          const model = hoverCardModel(tab)
          const shown = model.height - 2 * HOVER_CARD.margin
          // A Gmail row's card is the feed's mail card the frame cannot foresee, so it hangs level with the top too.
          const top = model.kind === 'mail' || isMailPreview(tab.url) ? r.top : r.top + r.height / 2 - shown / 2
          const at = { x: r.right + LIVE_PREVIEW.rowGap - HOVER_CARD.offsetX, y: top - HOVER_CARD.offsetY }
          // The browser draws the feed's message list whatever the title says, so any Gmail row's card can be reached.
          hoverEnter(tab.id, e.currentTarget, at, MAIL_PREVIEW.delayMs, isMailPreview(tab.url))
        } else hoverEnter(tab.id, e.currentTarget, { x: e.clientX, y: e.clientY })
      }}
      onPointerMove={preview ? undefined : (e) => hoverMove(e.clientX, e.clientY)}
      onPointerLeave={hoverPointerLeave}
      onPointerDown={(e) => {
        // A press (click or the start of a drag) hides the card.
        hoverLeave()
        activeAtPress.current = active
        if (e.button === 0 && !e.metaKey && !e.shiftKey && !e.ctrlKey && !active && !renaming) activate()
        drag.onPointerDown(e)
      }}
    >
      {v.offPinned ? (
        // No tooltip of its own: the subtitle under the title says where it goes, and a pill would cover it. A press
        // on it still selects and lifts the row; only a click without a drag resets it.
        <button type="button" aria-label="Back to Pinned URL" className="clu-icon-button clu-row-reset" tabIndex={-1} onClick={() => void clu().tabs.resetToPinned({ tabId: tab.id })}>
          <RowIcon tab={tab} title={v.title} />
        </button>
      ) : (
        <RowIcon tab={tab} title={v.title} />
      )}
      {v.offPinned ? (
        <span className="clu-row-slash" aria-hidden="true">
          /
        </span>
      ) : null}
      {renaming ? (
        <RenameField value={v.title} label="Tab name" onDone={rename} />
      ) : v.offPinned ? (
        // Hovering the favicon of a pinned tab that navigated away shows where it goes under the title.
        <span className="clu-row-title-stack">
          <span className="clu-row-title">{v.title}</span>
          <span className="clu-row-subtitle" aria-hidden="true">
            Back to Pinned URL
          </span>
        </span>
      ) : tidy ? (
        <span className="clu-row-title-stack" data-tidied="true">
          {morphing ? <MorphTitle text={v.title} /> : <span className="clu-row-title">{v.title}</span>}
          <span className="clu-row-subtitle">Double-click to rename</span>
        </span>
      ) : tab.section === 'pinned' && tab.subtitle ? (
        // A loaded Google Calendar pinned row shows its next event under the title.
        <span className="clu-row-title-stack" data-event="true">
          <span className="clu-row-title">{v.title}</span>
          <span className="clu-row-subtitle">{tab.subtitle}</span>
        </span>
      ) : (
        <span className="clu-row-title">{v.title}</span>
      )}
      {peekIcon ? <img className="clu-row-peek-icon" data-peek-icon="true" src={peekIcon} alt="" draggable={false} /> : null}
      {audio ? (
        <IconButton
          aria-label={v.audioLabel}
          className="clu-row-audio"
          data-audio={audio}
          tabIndex={-1}
          onPointerDown={keepPointer}
          onClick={(e) => {
            e.stopPropagation()
            void clu().tabs.setMuted({ tabId: tab.id, muted: !tab.muted })
          }}
        >
          {audio === 'muted' ? <SpeakerMutedIcon /> : <SpeakerIcon />}
        </IconButton>
      ) : null}
      <IconButton
        aria-label={v.close === 'remove' ? 'Remove from sidebar' : v.close === 'unload' ? 'Unload tab' : 'Close tab'}
        className="clu-row-close"
        data-close={v.close}
        tabIndex={-1}
        onPointerDown={keepPointer}
        onClick={(e) => {
          e.stopPropagation()
          if (v.close === 'remove') void clu().tabs.delete({ nodeId: tab.id })
          else void clu().tabs.close({ tabId: tab.id })
        }}
      >
        {v.close === 'unload' ? <MinusIcon /> : <CloseIcon />}
      </IconButton>
    </TreeRow>
  )
}
