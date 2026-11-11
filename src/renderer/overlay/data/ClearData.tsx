// Clear browsing data dialog: profile, time range and six data types, Cancel and "Clear data".
// Chromium's BrowsingDataRemover clears every type by the range, cookies and cache included. A spinner shows while clearing;
// the browser shows the toast, then the dialog closes itself.
import { useId, useState, type KeyboardEvent } from 'react'
import type { CluState } from '../../../shared/state'
import { Dialog } from '../../common/Dialog'
import { useCluState } from '../../common/useCluState'
import { closeInstance, type SurfaceProps } from '../core/types'
import './clear-data.css'
import { clu } from '../../common/clu'
import { Button, Select } from '../../common/controls'

export type ClearRange = 'hour' | 'day' | 'week' | 'month' | 'all'
export type ClearType = 'history' | 'cookies' | 'cache' | 'downloads' | 'permissions' | 'archive'

export const RANGES: readonly { value: ClearRange; label: string }[] = [
  { value: 'hour', label: 'Last hour' },
  { value: 'day', label: 'Last 24 hours' },
  { value: 'week', label: 'Last 7 days' },
  { value: 'month', label: 'Last 4 weeks' },
  { value: 'all', label: 'All time' }
]
export const TYPES: readonly { value: ClearType; label: string }[] = [
  { value: 'history', label: 'Browsing history' },
  { value: 'cookies', label: 'Cookies and site data' },
  { value: 'cache', label: 'Cached images and files' },
  { value: 'downloads', label: 'Download history' },
  { value: 'permissions', label: 'Site permissions' },
  { value: 'archive', label: 'Archived tabs' }
]
export const DEFAULT_TYPES: Record<ClearType, boolean> = { history: true, cookies: true, cache: true, downloads: false, permissions: false, archive: false }

interface ProfileOption { id: string; name: string }
const NONE: ProfileOption[] = []

/** Persistent profiles by name, plus the dialog's own profile when it is a private one. */
function profileOptions(s: CluState, own: string | null): ProfileOption[] {
  const list = Object.values(s.profiles).filter((p) => p && typeof p.id === 'string' && (!p.ephemeral || p.id === own))
  if (list.length === 0) return NONE
  return list
    .map((p) => ({ id: p.id, name: typeof p.name === 'string' && p.name.trim() ? p.name : 'Unnamed profile' }))
    .sort((a, b) => a.name.localeCompare(b.name) || a.id.localeCompare(b.id))
}
const sameOptions = (a: ProfileOption[], b: ProfileOption[]) => a.length === b.length && a.every((o, i) => o.id === b[i]?.id && o.name === b[i]?.name)

const FOCUSABLE = 'button:not(:disabled), input:not(:disabled), select:not(:disabled)'

export function ClearDataSurface({ instance }: SurfaceProps) {
  const props = (instance.props ?? {}) as { profileId?: unknown }
  const own = typeof props.profileId === 'string' ? props.profileId : null
  const options = useCluState((s) => profileOptions(s, own), sameOptions)
  const [picked, setPicked] = useState<string | null>(own)
  const profileId = picked && options.some((o) => o.id === picked) ? picked : (options[0]?.id ?? null)
  const [range, setRange] = useState<ClearRange>('hour')
  const [types, setTypes] = useState(DEFAULT_TYPES)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const id = useId()
  const any = Object.values(types).some(Boolean)

  const cancel = () => {
    if (!busy) closeInstance(instance.instanceId)
  }
  const clear = async () => {
    if (busy || !any || !profileId) return
    setBusy(true)
    setError(null)
    const r = await clu().browsingData.clear({ profileId, range, types })
    if (r.ok) {
      closeInstance(instance.instanceId)
      return
    }
    setBusy(false)
    setError("Rio couldn't clear browsing data. Try again.")
  }
  // Dialog's own trap skips selects, so this one keeps Tab inside every control.
  const trap = (e: KeyboardEvent<HTMLDivElement>) => {
    if (e.key !== 'Tab') return
    e.stopPropagation()
    const nodes = [...e.currentTarget.querySelectorAll<HTMLElement>(FOCUSABLE)]
    const first = nodes[0]
    const last = nodes[nodes.length - 1]
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault()
      last?.focus()
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault()
      first?.focus()
    }
  }

  return (
    <div className="clu-center clu-clear-data" data-surface="clear-data" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="Clear browsing data" onCancel={cancel}>
        <div onKeyDown={trap} aria-busy={busy || undefined}>
          {options.length > 1 && (
            <div className="clu-clear-field">
              <label htmlFor={`${id}-profile`}>Profile</label>
              <Select id={`${id}-profile`} value={profileId ?? ''} disabled={busy} onChange={(e) => setPicked(e.currentTarget.value)}>
                {options.map((o) => (
                  <option key={o.id} value={o.id}>
                    {o.name}
                  </option>
                ))}
              </Select>
            </div>
          )}
          <div className="clu-clear-field">
            <label htmlFor={`${id}-range`}>Time range</label>
            <Select id={`${id}-range`} value={range} disabled={busy} onChange={(e) => setRange(e.currentTarget.value as ClearRange)}>
              {RANGES.map((r) => (
                <option key={r.value} value={r.value}>
                  {r.label}
                </option>
              ))}
            </Select>
          </div>
          <fieldset className="clu-clear-types" disabled={busy}>
            <legend className="clu-clear-legend">Data to clear</legend>
            {TYPES.map((t) => (
              <label key={t.value} className="clu-clear-type">
                <input type="checkbox" checked={types[t.value]} onChange={(e) => setTypes({ ...types, [t.value]: e.currentTarget.checked })} />
                <span>{t.label}</span>
              </label>
            ))}
          </fieldset>
          {error && (
            <p className="clu-clear-error" role="alert">
              {error}
            </p>
          )}
          <div className="clu-clear-actions">
            <Button disabled={busy} onClick={cancel}>
              Cancel
            </Button>
            <Button variant="danger" disabled={busy || !any || !profileId} onClick={() => void clear()}>
              {busy && <span className="clu-clear-spinner" aria-hidden="true" />}
              {busy ? 'Clearing…' : 'Clear data'}
            </Button>
          </div>
        </div>
      </Dialog>
    </div>
  )
}
