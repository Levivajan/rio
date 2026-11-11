// The profile that profile-scoped settings controls edit. One choice for the whole page, so
// the General and Archive pickers agree. Private profiles are left out. A choice that no longer exists falls back to
// the default profile, then the first by name.
import { useCallback, useSyncExternalStore } from 'react'
import type { CluState } from '../../../../shared/state'
import { useCluState } from '../../../common/useCluState'

export interface ProfileOption { id: string; name: string }

let chosen: string | null = null
const listeners = new Set<() => void>()
const subscribe = (fn: () => void) => {
  listeners.add(fn)
  return () => listeners.delete(fn)
}
const getChosen = () => chosen

/** Tests reset the page-wide choice between cases. */
export function resetProfileChoice(): void {
  chosen = null
  listeners.forEach((fn) => fn())
}

export function profileOptions(s: CluState): { options: ProfileOption[]; defaultId: string | null } {
  const list = Object.values(s.profiles).filter((p) => p && !p.ephemeral && typeof p.id === 'string')
  const options = list
    .map((p) => ({ id: p.id, name: typeof p.name === 'string' && p.name.trim() ? p.name : 'Unnamed profile' }))
    .sort((a, b) => a.name.localeCompare(b.name) || a.id.localeCompare(b.id))
  return { options, defaultId: list.find((p) => p.isDefault)?.id ?? options[0]?.id ?? null }
}

const sameOptions = (a: ReturnType<typeof profileOptions>, b: ReturnType<typeof profileOptions>) =>
  a.defaultId === b.defaultId && a.options.length === b.options.length && a.options.every((o, i) => o.id === b.options[i]?.id && o.name === b.options[i]?.name)

export function useSettingsProfile(): { options: ProfileOption[]; profileId: string | null; choose(id: string): void } {
  const { options, defaultId } = useCluState(profileOptions, sameOptions)
  const pick = useSyncExternalStore(subscribe, getChosen, getChosen)
  const profileId = pick && options.some((o) => o.id === pick) ? pick : defaultId
  const choose = useCallback((id: string) => {
    chosen = id
    listeners.forEach((fn) => fn())
  }, [])
  return { options, profileId, choose }
}
