// Sidebar media bar: the tab main picked for windows[id].mediaTabId, the most
// recently started audible tab out of sight, as a 33 pt white bar with no title: its favicon brings the
// tab back (media.focus, named by the title), previous and next track go to the page's media session (media.previous,
// media.next), the middle button pauses and plays it (media.toggle) and the last one mutes it (tabs.setMuted).
import type { SVGProps } from 'react'
import { IconButton } from '../../common/IconButton'
import { LetterTile } from '../../common/LetterTile'
import { useCluState, useWindowId } from '../../common/useCluState'
import './split.css'
import { clu } from '../../common/clu'

const icon = { width: 16, height: 16, viewBox: '0 0 16 16', 'aria-hidden': true } as const
const PlayIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...icon} fill="currentColor" {...p}><path d="M5 3.4v9.2a.6.6 0 0 0 .9.5l7.3-4.6a.6.6 0 0 0 0-1L5.9 2.9a.6.6 0 0 0-.9.5Z" /></svg>
)
const PauseIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...icon} fill="currentColor" {...p}><rect x="4" y="3" width="2.8" height="10" rx=".8" /><rect x="9.2" y="3" width="2.8" height="10" rx=".8" /></svg>
)
/** Previous and next track, 13.5 x 11: a bar and a triangle. */
const PreviousIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...icon} fill="currentColor" {...p}><rect x="1.25" y="2.5" width="1.8" height="11" rx=".7" /><path d="M14.75 3.3v9.4a.6.6 0 0 1-.9.5L4.9 8.5a.6.6 0 0 1 0-1l8.95-4.7a.6.6 0 0 1 .9.5Z" /></svg>
)
const NextIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...icon} fill="currentColor" {...p}><rect x="12.95" y="2.5" width="1.8" height="11" rx=".7" /><path d="M1.25 3.3v9.4a.6.6 0 0 0 .9.5l8.95-4.7a.6.6 0 0 0 0-1L2.15 2.8a.6.6 0 0 0-.9.5Z" /></svg>
)
const SpeakerIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...icon} fill="none" stroke="currentColor" strokeWidth={1.6} strokeLinecap="round" strokeLinejoin="round" {...p}>
    <path d="M2.5 6v4h2.5l3.5 3V3L5 6Z" />
    <path d="M11 5.8a3 3 0 0 1 0 4.4M12.8 4a5.5 5.5 0 0 1 0 8" />
  </svg>
)
const MutedIcon = (p: SVGProps<SVGSVGElement>) => (
  <svg {...icon} fill="none" stroke="currentColor" strokeWidth={1.6} strokeLinecap="round" strokeLinejoin="round" {...p}>
    <path d="M2.5 6v4h2.5l3.5 3V3L5 6Z" />
    <path d="m11 6 3 4m0-4-3 4" />
  </svg>
)

export function MediaCard() {
  const windowId = useWindowId() ?? ''
  // One selector: the hook caches per state object, so a second one keyed on the first's result would go stale.
  const tab = useCluState((s) => {
    const tabId = s.windows[windowId]?.mediaTabId
    return tabId ? s.tabs[tabId] : undefined
  })
  if (!tab) return null
  const title = tab.titleOverride || tab.title || tab.url
  const playing = tab.mediaPlaying
  return (
    <section
      className="clu-media-card"
      aria-label="Now playing"
      data-media-tab-id={tab.id}
      data-state={playing ? 'playing' : 'paused'}
      data-muted={String(tab.muted)}
    >
      <button type="button" className="clu-media-title" aria-label={title} onClick={() => void clu().media.focus({ tabId: tab.id })}>
        {tab.favicon ? <img className="clu-row-favicon" src={tab.favicon} alt="" draggable={false} /> : <LetterTile text={title} />}
      </button>
      <IconButton aria-label="Previous track" className="clu-media-previous" onClick={() => void clu().media.previous({ tabId: tab.id })}>
        <PreviousIcon />
      </IconButton>
      <IconButton aria-label={playing ? 'Pause' : 'Play'} className="clu-media-toggle" onClick={() => void clu().media.toggle({ tabId: tab.id })}>
        {playing ? <PauseIcon /> : <PlayIcon />}
      </IconButton>
      <IconButton aria-label="Next track" className="clu-media-next" onClick={() => void clu().media.next({ tabId: tab.id })}>
        <NextIcon />
      </IconButton>
      <IconButton aria-label={tab.muted ? 'Unmute tab' : 'Mute tab'} className="clu-media-mute" data-muted={String(tab.muted)} onClick={() => void clu().tabs.setMuted({ tabId: tab.id, muted: !tab.muted })}>
        {tab.muted ? <MutedIcon /> : <SpeakerIcon />}
      </IconButton>
    </section>
  )
}
