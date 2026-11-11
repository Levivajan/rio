// One setting for a settings control: the resolved value from state (default when missing or
// invalid), saved on change through settings.set. While a write is in flight the control shows the new value; a
// rejected write drops it, so the control reverts to the stored value, and leaves an inline error until the next change.
import { useCallback, useEffect, useRef, useState } from 'react'
import { readSetting } from '../../../../shared/settings'
import type { CluState } from '../../../../shared/state'
import type { Json } from '../../../../shared/types'
import { clu } from '../../../common/clu'
import { invoke } from '../../../common/invoke'
import { useCluState } from '../../../common/useCluState'

export const saveError = (code: string, disabled: boolean): string => {
  if (disabled) return 'Settings can’t be saved right now.'
  if (code === 'bad-payload') return 'That value isn’t allowed, so the previous setting is kept.'
  if (code === 'not-found') return 'This profile no longer exists.'
  return 'Couldn’t save this setting. Try again.'
}

const same = (a: Json, b: Json) => a === b || JSON.stringify(a) === JSON.stringify(b)

export function storedValue(settings: CluState['settings'], key: string, scope: string | null): Json {
  const raw = scope === null ? undefined : scope === 'global' ? settings.global[key] : settings.byProfile[scope]?.[key]
  return readSetting(key, raw) ?? null
}

const selectSettings = (s: CluState) => s.settings

export function useSetting(key: string, scope: string | null) {
  // useCluState caches per state object, so the selector stays fixed and the key and scope apply outside it.
  const stored = storedValue(useCluState(selectSettings), key, scope)
  // `settled` marks a write main accepted; the state patch that follows replaces it.
  const [pending, setPending] = useState<{ value: Json; settled: boolean } | null>(null)
  const [error, setError] = useState<string | null>(null)
  const seq = useRef(0)
  const latest = useRef(stored)
  latest.current = stored

  useEffect(() => {
    setPending((p) => (p?.settled ? null : p))
  }, [stored])
  useEffect(() => {
    seq.current++
    setPending(null)
    setError(null)
  }, [key, scope])

  const set = useCallback(
    (value: Json) => {
      if (!scope) return
      const mine = ++seq.current
      setPending({ value, settled: false })
      setError(null)
      void invoke(clu().settings.set({ key, scope, value })).then((r) => {
        if (mine !== seq.current) return
        if (r.ok) setPending(same(latest.current, value) ? null : { value, settled: true })
        else {
          setPending(null)
          setError(saveError(r.code, r.disabled))
        }
      })
    },
    [key, scope]
  )

  return { value: pending ? pending.value : stored, error, set }
}
