// Polls the process list and the hibernated tabs for clu://about.
import { useEffect, useState } from 'react'
import type { HibernationRow } from '../../../shared/ipc/capabilities'
import type { ProcessRow } from '../../../shared/ipc/platform'
import { invoke } from '../../common/invoke'
import { clu } from '../../common/clu'

/** null while loading, 'unavailable' when the command fails or isn't implemented yet. */
export type Loaded<T> = T[] | 'unavailable' | null

export const ABOUT_POLL_MS = 2000

export function useAboutData(pollMs = ABOUT_POLL_MS): { processes: Loaded<ProcessRow>; hibernated: Loaded<HibernationRow> } {
  const [processes, setProcesses] = useState<Loaded<ProcessRow>>(null)
  const [hibernated, setHibernated] = useState<Loaded<HibernationRow>>(null)
  useEffect(() => {
    let alive = true
    const load = async () => {
      const [p, h] = await Promise.all([invoke(clu().platform.processes()), invoke(clu().hibernation.list())])
      if (!alive) return
      setProcesses(p.ok && Array.isArray(p.value) ? p.value : 'unavailable')
      setHibernated(h.ok && Array.isArray(h.value) ? h.value : 'unavailable')
    }
    void load()
    const timer = setInterval(() => void load(), pollMs)
    return () => {
      alive = false
      clearInterval(timer)
    }
  }, [pollMs])
  return { processes, hibernated }
}
