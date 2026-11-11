// The card shown after Create Space: 🙌 on a white card over a soft glow, centered in the sidebar,
// for about a second; then the new, empty space shows. It plays when this window switches to a space that joined the
// list moments before (the browser adds the space, then switches the window that made it).
// creatingSpace tells the bottom bar while the New Space panel (overlay CreateSpacePanel.tsx) is open.
import { useEffect, useRef, useState } from 'react'
import { SPACE_EDITOR_SURFACE } from '../../../shared/spaces'
import type { CluState } from '../../../shared/state'
import { sameItems, useCluState } from '../../common/useCluState'

/** True while `windowId` shows the New Space panel: the space editor without a space, on its form. */
export function creatingSpace(s: CluState, windowId: string): boolean {
  return (s.windows[windowId]?.overlay.surfaces ?? []).some((i) => {
    if (i.surface !== SPACE_EDITOR_SURFACE) return false
    const props = i.props && typeof i.props === 'object' && !Array.isArray(i.props) ? i.props : {}
    return typeof props.spaceId !== 'string' && props.view !== 'profiles'
  })
}

/** How long the card shows. */
export const CARD_MS = 1000
/** A space this new (since it joined the list) counts as just made when the window switches to it. */
export const FRESH_MS = 2000

/** The ids in `order` that `seen` (id to the time it joined) does not know yet get `now`; ids gone are dropped. */
export function noteSpaces(seen: Map<string, number>, order: readonly string[], now: number, first: boolean): void {
  const keep = new Set(order)
  for (const id of [...seen.keys()]) if (!keep.has(id)) seen.delete(id)
  // The spaces there when the sidebar first renders are not new.
  for (const id of order) if (!seen.has(id)) seen.set(id, first ? -Infinity : now)
}

/** True while the card for a just-made `active` space should show. */
export function useNewSpaceCard(active: string): boolean {
  const order = useCluState((s) => s.spaceOrder, sameItems)
  const seen = useRef<Map<string, number> | null>(null)
  const last = useRef(active)
  const [card, setCard] = useState<string | null>(null)
  // Declared first, so a patch that adds the space and switches to it in one go is noted before the switch is read.
  useEffect(() => {
    const first = seen.current === null
    seen.current ??= new Map()
    noteSpaces(seen.current, order, Date.now(), first)
  }, [order])
  useEffect(() => {
    if (active === last.current) return
    last.current = active
    const joined = seen.current?.get(active) ?? -Infinity
    if (Date.now() - joined > FRESH_MS) return
    setCard(active)
    const t = setTimeout(() => setCard(null), CARD_MS)
    return () => clearTimeout(t)
  }, [active])
  return card !== null && card === active
}

export function NewSpaceCard() {
  return (
    <div className="clu-new-space-card" role="status" aria-label="Space created">
      <span className="clu-new-space-card-tile" aria-hidden="true">🙌</span>
    </div>
  )
}
