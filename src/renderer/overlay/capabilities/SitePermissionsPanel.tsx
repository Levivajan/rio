// Site permissions section of the Site controls popover (engine 2.9): camera, microphone,
// location, notifications, clipboard and pop-ups for the tab's origin, then any other permission the site has a
// decision for (sound, MIDI, sensors...), each Ask, Allow or Block, read and written through permission.*, which the
// browser keeps in Chromium's own site settings (docs/chromium/fixes.md). A change on a live page shows "Reload to
// apply".
import { useCallback, useEffect, useState } from 'react'
import { SITE_PERMISSIONS, originHost, permissionLabel, type PermissionRow } from '../../../shared/capabilities'
import { useCluState } from '../../common/useCluState'
import './permissions.css'
import { clu } from '../../common/clu'
import { invoke } from '../../common/invoke'
import { Button, Select } from '../../common/controls'

type Choice = 'ask' | 'allow' | 'block'

function webOrigin(url: string | undefined): string | null {
  try {
    const u = new URL(url ?? '')
    return u.protocol === 'http:' || u.protocol === 'https:' ? u.origin : null
  } catch {
    return null
  }
}

export function SitePermissionsPanel({ tabId }: { tabId: string }) {
  const url = useCluState((s) => s.tabs[tabId]?.url)
  const profileId = useCluState((s) => s.tabs[tabId]?.profileId ?? '')
  const origin = webOrigin(url)
  const [rows, setRows] = useState<PermissionRow[]>([])
  const [changed, setChanged] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const load = useCallback(async () => {
    if (!origin || !profileId) return
    const r = await invoke(clu().permission.list({ profileId, origin }))
    if (r.ok) setRows(r.value)
    else setError('Could not read permissions')
  }, [origin, profileId])
  useEffect(() => {
    void load()
  }, [load])

  if (!origin) return null
  // The six every site shows, then the others this site has a decision for, in the browser's order.
  const shown: string[] = [...SITE_PERMISSIONS, ...[...new Set(rows.map((r) => r.permission))].filter((p) => !(SITE_PERMISSIONS as readonly string[]).includes(p))]
  const choice = (p: string): Choice => {
    const d = rows.find((r) => r.permission === p)?.decision
    return d === 'allow' ? 'allow' : d === 'deny' ? 'block' : 'ask'
  }
  const after = async (r: { ok: boolean }) => {
    if (!r.ok) setError('Could not save the change')
    else setError(null)
    setChanged(true)
    await load()
  }
  const change = async (permission: string, next: Choice) => {
    if (next === 'ask') await after(await clu().permission.clear({ profileId, origin, permission }))
    else await after(await clu().permission.set({ profileId, origin, permission, decision: next }))
  }
  const reset = async () => after(await clu().permission.clear({ profileId, origin }))

  return (
    <section className="clu-site-permissions" aria-label={`Permissions for ${originHost(origin)}`} data-origin={origin}>
      <ul>
        {shown.map((p) => (
          <li key={p} className="clu-site-permission" data-permission={p} data-decision={choice(p)}>
            <label htmlFor={`site-perm-${p}`}>{permissionLabel(p)}</label>
            <Select id={`site-perm-${p}`} value={choice(p)} onChange={(e) => void change(p, e.target.value as Choice)}>
              <option value="ask">Ask</option>
              <option value="allow">Allow</option>
              <option value="block">Block</option>
            </Select>
          </li>
        ))}
      </ul>
      <div className="clu-site-permissions-footer">
        {changed ? (
          <button type="button" className="clu-link-button" onClick={() => void clu().nav.reload({ tabId })}>
            Reload to apply
          </button>
        ) : <span />}
        <Button disabled={rows.length === 0} onClick={() => void reset()}>
          Reset permissions
        </Button>
      </div>
      {error ? <p className="clu-site-permissions-error" role="alert">{error}</p> : null}
    </section>
  )
}
