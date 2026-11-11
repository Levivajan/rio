// Tidy Today's broom (docs/chromium/parity-command.md, ai.tidyTabs): on the Today divider beside Clear, while
// Today holds more than six tabs. The browser first groups Today by topic with the on-device model (tabs.tidyToday);
// without one, the sidebar's rules move Today into site order in one tabs.bulk move. Either way the rows slide into
// place with the sidebar's usual reorder animation.
import { AI, aiOn } from '../../../shared/ai'
import { MAX_BULK } from '../../../shared/tabs'
import { TIDY_MIN_TODAY, tidyOrder } from '../../../shared/tidy'
import { clu } from '../../common/clu'
import { stateMirror } from '../../common/store'
import { useCluState } from '../../common/useCluState'
import './tidy.css'

const BroomIcon = () => (
  <svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d="M13.5 2.5 8.6 7.4" />
    <path d="M7.4 6.6 9.4 8.6c.4.4.4 1 0 1.4l-.6.6-3.4-3.4.6-.6c.4-.4 1-.4 1.4 0Z" />
    <path d="M5.4 7.2 2.5 10.1c1 1.8 2.2 3 4 3.9l2.9-2.9" />
    <path d="m4.2 11.9 1.4-1.4M6.1 13.2l1-1" />
  </svg>
)

export function TidyButton({ spaceId, count }: { spaceId: string; count: number }) {
  const on = useCluState((s) => aiOn(s.settings.global, AI.tidyTabs))
  if (!on || count < TIDY_MIN_TODAY) return null
  const tidy = async () => {
    const asked = await clu().tabs.tidyToday({ spaceId })
    if (!asked.ok || asked.value.status !== 'no-model') return
    const s = stateMirror().get()
    const today = s.spaces[spaceId]?.today ?? []
    const order = tidyOrder(today.map((id) => ({ id, url: s.tabs[id]?.url ?? '', splitId: s.tabs[id]?.splitId ?? null })))
    if (!order) return
    void clu().tabs.bulk({ tabIds: order.slice(0, MAX_BULK), action: 'move', target: { section: 'today', spaceId, index: 0 } })
  }
  return (
    <button type="button" className="clu-today-tidy" data-tidy-today="true" aria-label="Tidy Today" onClick={() => void tidy()}>
      <BroomIcon />
      Tidy
    </button>
  )
}
