// clu://extensions logic (docs/plan/08-extensions.md F4): the chosen profile's extensions from state,
// and the actions on them. Sessions are lazy, so extensions.list wakes the chosen profile's session; its load then
// arrives through state like every later change.
import { useCallback, useEffect, useState } from 'react'
import type { ExtensionInfo } from '../../../shared/ipc/extensions'
import type { CluState } from '../../../shared/state'
import { invoke } from '../../common/invoke'
import { sameItems, useCluState } from '../../common/useCluState'
import { clu } from '../../common/clu'

export interface ProfileOption { id: string; name: string }

const NONE: ExtensionInfo[] = []

/** Saved profiles, the default first: a private window has no extensions. */
export function profileOptions(s: CluState): ProfileOption[] {
  return Object.values(s.profiles)
    .filter((p) => !p.ephemeral)
    .sort((a, b) => Number(b.isDefault) - Number(a.isDefault) || a.name.localeCompare(b.name))
    .map((p) => ({ id: p.id, name: p.name || 'Profile' }))
}
const sameOptions = (a: ProfileOption[], b: ProfileOption[]) => sameItems(a.map((p) => `${p.id}\n${p.name}`), b.map((p) => `${p.id}\n${p.name}`))

const selectProfiles = (s: CluState) => s.profiles

export function useExtensionsPage() {
  const profiles = useCluState(profileOptions, sameOptions)
  const [picked, setPicked] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const profileId = profiles.find((p) => p.id === picked)?.id ?? profiles[0]?.id ?? null
  const byId = useCluState(selectProfiles)
  const extensions = (profileId ? byId[profileId]?.extensions : null) ?? NONE

  useEffect(() => {
    void invoke(clu().extensions.pageProfile()).then((r) => {
      if (r.ok && r.value.profileId) setPicked(r.value.profileId)
    })
  }, [])

  useEffect(() => {
    if (!profileId) return
    setError(null)
    void invoke(clu().extensions.list({ profileId })).then((r) => {
      if (!r.ok) setError('Couldn’t load extensions.')
    })
  }, [profileId])

  const act = useCallback(async (run: () => ReturnType<typeof invoke>, failure: string) => {
    setError(null)
    if (!(await run()).ok) setError(failure)
  }, [])

  return {
    profiles,
    profileId,
    extensions,
    error,
    pickProfile: setPicked,
    setEnabled: (id: string, enabled: boolean) => (profileId ? act(() => invoke(clu().extensions.setEnabled({ profileId, id, enabled })), `Couldn’t turn the extension ${enabled ? 'on' : 'off'}. Try again.`) : Promise.resolve()),
    remove: (id: string) => (profileId ? act(() => invoke(clu().extensions.remove({ profileId, id })), 'Couldn’t remove the extension. Try again.') : Promise.resolve()),
    openOptions: (id: string) => (profileId ? act(() => invoke(clu().extensions.openOptions({ profileId, id })), 'Couldn’t open its options.') : Promise.resolve()),
    openStore: () => act(() => invoke(clu().extensions.openStore()), 'Couldn’t open the Chrome Web Store.')
  }
}
