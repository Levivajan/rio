// The Site tweaks list for one profile (tweaks-t4): loads tweaks.list, refreshes whenever an open tab's
// tweak changes, and toggles or deletes one site at a time.
import { useCallback, useEffect, useRef, useState } from 'react'
import type { CluState } from '../../../shared/state'
import type { SiteTweakRow } from '../../../shared/tweaks'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import { clu } from '../../common/clu'

/** Changes whenever a tab of the profile shows a different tweak (a save, zap or toggle made in a tab). */
export function tweakSignature(s: CluState, profileId: string): string {
  const out: string[] = []
  for (const t of Object.values(s.tabs)) {
    if (t.profileId !== profileId || !t.tweak) continue
    const w = t.tweak
    out.push(`${t.id}:${w.enabled ? 1 : 0}${w.hasCss ? 1 : 0}${w.hasJs ? 1 : 0}${w.hasControls ? 1 : 0}${w.zapCount}`)
  }
  return out.sort().join(',')
}

export interface SiteTweaks {
  /** null until the first load. */
  rows: SiteTweakRow[] | null
  error: string | null
  setEnabled(site: string, enabled: boolean): Promise<void>
  remove(site: string): Promise<boolean>
}

export function useSiteTweaks(profileId: string): SiteTweaks {
  const [rows, setRows] = useState<SiteTweakRow[] | null>(null)
  const [error, setError] = useState<string | null>(null)
  const seq = useRef(0)
  const signature = useCluState(useCallback((s: CluState) => tweakSignature(s, profileId), [profileId]))

  const refresh = useCallback(async () => {
    const n = ++seq.current
    const r = await invoke(clu().tweaks.list({ profileId }))
    // A later refresh owns the list.
    if (n !== seq.current) return
    if (r.ok) setRows(Array.isArray(r.value) ? r.value : [])
    else {
      setRows((prev) => prev ?? [])
      setError('Couldn’t load site tweaks.')
    }
  }, [profileId])

  useEffect(() => {
    void refresh()
  }, [refresh, signature])

  const setEnabled = useCallback(
    async (site: string, enabled: boolean) => {
      setError(null)
      setRows((prev) => prev?.map((r) => (r.site === site ? { ...r, enabled } : r)) ?? prev)
      const r = await invoke(clu().tweaks.setEnabledForSite({ profileId, site, enabled }))
      if (!r.ok) setError(`Couldn’t turn site tweaks ${enabled ? 'on' : 'off'}. Try again.`)
      await refresh()
    },
    [profileId, refresh]
  )

  const remove = useCallback(
    async (site: string) => {
      setError(null)
      const r = await invoke(clu().tweaks.deleteForSite({ profileId, site }))
      if (!r.ok) setError('Couldn’t delete the site tweak. Try again.')
      await refresh()
      return r.ok
    },
    [profileId, refresh]
  )

  return { rows, error, setEnabled, remove }
}
