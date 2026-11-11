// The icon slot of a row or favorite tile (design.md section 2): custom emoji, favicon or
// letter tile, cross-faded on change. The row never spins: a page still loading without its own favicon shows a 14 pt
// square in the light space accent, and the progress shows above the page card (CardLoading). The cross-fade runs
// only when the icon changes under a mounted row: a row that mounts (pinned, unpinned, a folder that opens, a space
// switch) shows its icon at once.
import { useRef, useState } from 'react'
import type { TabState } from '../../../shared/state'
import { LetterTile } from '../../common/LetterTile'

export function RowIcon({ tab, title, size }: { tab: TabState; title: string; size?: number }) {
  const [broken, setBroken] = useState<string | null>(null)
  const dim = size ? { width: size, height: size } : undefined
  const favicon = tab.favicon && broken !== tab.favicon ? tab.favicon : null
  const shown = tab.customIcon ?? favicon ?? ''
  const first = useRef(shown)
  const fresh = shown !== first.current ? true : undefined
  // A live page's icon URL carries cluIcon= once Chromium has the page's own icon; until then it would draw the
  // default globe.
  if (tab.loading && !tab.customIcon && (!favicon || (favicon.startsWith('chrome://favicon2/') && !favicon.includes('cluIcon=')))) return <span className="clu-row-favicon-slot" data-loading-icon="true" aria-hidden="true" style={dim} />
  if (tab.customIcon) {
    return (
      <span key={tab.customIcon} className="clu-row-favicon clu-row-emoji" data-fresh={fresh} aria-hidden="true" style={dim}>
        {tab.customIcon}
      </span>
    )
  }
  if (favicon) {
    const src = favicon
    return <img key={src} className="clu-row-favicon" data-fresh={fresh} src={src} alt="" draggable={false} style={dim} onError={() => setBroken(src)} />
  }
  return <LetterTile text={title} {...(size ? { size } : {})} />
}
