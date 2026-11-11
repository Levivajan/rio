// Split row: one sidebar row for a split, its members' favicons and titles in pane order separated by
// thin dividers. Clicking brings the whole split back with its focused pane. Inside a folder it takes the folder's
// indent, as a tab row does.
import { leaves } from '../../../shared/split'
import type { TabState } from '../../../shared/state'
import { LetterTile } from '../../common/LetterTile'
import { TreeRow } from '../../common/TreeRow'
import { indent } from '../tabs/FolderRow'
import { useCluState } from '../../common/useCluState'
import './split.css'
import { clu } from '../../common/clu'

export function SplitSidebarRow({ tabs, activeTabId, level = 1 }: { tabs: TabState[]; activeTabId: string | null; level?: number }) {
  const splitId = tabs[0]?.splitId ?? ''
  const split = useCluState((s) => s.splits[splitId])
  const first = tabs[0]
  if (!first) return null
  const order = split ? leaves(split.root) : []
  const members = [...tabs].sort((a, b) => order.indexOf(a.id) - order.indexOf(b.id))
  const active = tabs.some((t) => t.id === activeTabId)
  const target = split && tabs.some((t) => t.id === split.focusedTabId) ? split.focusedTabId : first.id
  const label = members.map((t) => t.titleOverride || t.title || t.url).join(', ')
  return (
    <TreeRow
      selected={active}
      level={level}
      style={indent(level)}
      aria-label={`Split view: ${label}`}
      className="clu-split-row"
      data-active={String(active)}
      data-split-id={splitId}
      onClick={() => void clu().tabs.activate({ tabId: target })}
      onKeyDown={(e) => {
        if (e.key === 'Enter') void clu().tabs.activate({ tabId: target })
      }}
    >
      {members.map((t, i) => {
        const title = t.titleOverride || t.title || t.url
        return (
          <span key={t.id} className="clu-split-member" data-split-member={t.id} data-first={String(i === 0)}>
            {t.favicon ? <img className="clu-row-favicon" src={t.favicon} alt="" draggable={false} /> : <LetterTile text={title} />}
            <span className="clu-split-title">{title}</span>
          </span>
        )
      })}
    </TreeRow>
  )
}
