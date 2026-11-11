// Site tweaks in the app settings' Advanced tab (tweaks-t4): every site with a tweak in the chosen profile, with
// what it holds, an on/off switch and Delete behind a confirm.
import { useCallback, useId, useState } from 'react'
import type { CluState } from '../../../shared/state'
import { tweakParts, type SiteTweakRow } from '../../../shared/tweaks'
import { Dialog } from '../../common/Dialog'
import { sameItems, useCluState } from '../../common/useCluState'
import { useSiteTweaks } from './useSiteTweaks'
import './site-tweaks.css'
import { Button, Card, Row, Select, Toggle } from '../../common/controls'

interface ProfileOption { id: string; name: string }

/** Saved profiles, the default first; private profiles keep no tweaks worth listing. */
export function profileOptions(s: CluState): ProfileOption[] {
  return Object.values(s.profiles)
    .filter((p) => !p.ephemeral)
    .sort((a, b) => Number(b.isDefault) - Number(a.isDefault) || a.name.localeCompare(b.name))
    .map((p) => ({ id: p.id, name: p.name || 'Profile' }))
}
const sameOptions = (a: ProfileOption[], b: ProfileOption[]) => sameItems(a.map((p) => `${p.id}\n${p.name}`), b.map((p) => `${p.id}\n${p.name}`))

export function SiteTweaksSection() {
  const profiles = useCluState(profileOptions, sameOptions)
  const [picked, setPicked] = useState<string | null>(null)
  const profileId = profiles.find((p) => p.id === picked)?.id ?? profiles[0]?.id ?? null
  const pickerId = useId()
  if (!profileId) return null
  return (
    <div className="clu-site-tweaks">
      {profiles.length > 1 && (
        <Card>
          <Row label="Profile" htmlFor={pickerId}>
            <Select id={pickerId} value={profileId} onChange={(e) => setPicked(e.target.value)}>
              {profiles.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.name}
                </option>
              ))}
            </Select>
          </Row>
        </Card>
      )}
      <SiteTweakList key={profileId} profileId={profileId} />
    </div>
  )
}

export function SiteTweakList({ profileId }: { profileId: string }) {
  const { rows, error, setEnabled, remove } = useSiteTweaks(profileId)
  const [confirming, setConfirming] = useState<string | null>(null)
  const close = useCallback(() => setConfirming(null), [])
  if (rows === null) return null
  return (
    <>
      {error && (
        <p className="clu-site-tweaks-error" role="alert">
          {error}
        </p>
      )}
      {rows.length === 0 ? (
        error ? null : (
          <Card>
            <Row label={<span className="clu-settings-empty">No site tweaks yet</span>} />
          </Card>
        )
      ) : (
        <ul className="clu-group clu-settings-list" aria-label="Sites with tweaks">
          {rows.map((r) => (
            <SiteTweakItem key={r.site} row={r} onToggle={(on) => void setEnabled(r.site, on)} onDelete={() => setConfirming(r.site)} />
          ))}
        </ul>
      )}
      {confirming !== null && (
        <div className="clu-site-tweaks-scrim">
          <Dialog title="Delete site tweak?" onCancel={close}>
            <p className="clu-site-tweaks-confirm-site" title={confirming}>
              {confirming}
            </p>
            <p className="clu-dialog-body">Its CSS, JS, style controls and hidden elements are removed from this site.</p>
            <div className="clu-dialog-actions">
              <Button onClick={close}>
                Cancel
              </Button>
              <Button
                variant="danger"
                onClick={() => {
                  const site = confirming
                  close()
                  void remove(site)
                }}
              >
                Delete
              </Button>
            </div>
          </Dialog>
        </div>
      )}
    </>
  )
}

function SiteTweakItem({ row, onToggle, onDelete }: { row: SiteTweakRow; onToggle: (on: boolean) => void; onDelete: () => void }) {
  return (
    <li className="clu-group-row clu-site-tweak" data-site-tweak={row.site} data-state={row.enabled ? 'on' : 'off'}>
      <div className="clu-group-row-text">
        <span className="clu-group-row-label clu-site-tweak-site" title={row.site}>
          {row.site}
        </span>
        <span className="clu-group-row-hint clu-site-tweak-detail">
          {row.enabled ? 'On' : 'Off'} · {tweakParts(row).join(', ')}
        </span>
      </div>
      <Toggle label={`Site tweaks for ${row.site}`} checked={row.enabled} onChange={onToggle} />
      <Button aria-label={`Delete site tweak for ${row.site}`} onClick={onDelete}>
        Delete
      </Button>
    </li>
  )
}
