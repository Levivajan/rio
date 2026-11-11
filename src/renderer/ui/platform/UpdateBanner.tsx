// The update card (docs/chromium/m12.md): once the browser's updater has an update downloaded and verified,
// a small card at the bottom of the sidebar names the version and offers Restart to update. Quitting Rio installs it
// too, so the close button hides the card for this version only (the browser stores it); a newer version brings the
// card back.
import { useState } from 'react'
import { clu } from '../../common/clu'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import './update.css'

export function UpdateBanner() {
  const status = useCluState((s) => s.app.update?.status ?? 'unavailable')
  const version = useCluState((s) => s.app.update?.version ?? null)
  const dismissed = useCluState((s) => s.app.update?.dismissed === true)
  const [installing, setInstalling] = useState(false)
  // Hidden at once on close, for the version it was closed for; app.update.dismissed keeps it hidden after that.
  const [closedFor, setClosedFor] = useState<string | null | undefined>(undefined)
  if (status !== 'ready' || dismissed || closedFor === version) return null
  const restart = async () => {
    setInstalling(true)
    const r = await invoke(clu().platform.installUpdate())
    // Rio quits when the install starts; anything else leaves the button usable.
    if (!r.ok || r.value.status !== 'installing') setInstalling(false)
  }
  const close = async () => {
    setClosedFor(version)
    const r = await invoke(clu().platform.dismissUpdate())
    // Not stored (nothing ready after all, or the call failed): the card follows app.update again.
    if (!r.ok || r.value.status !== 'dismissed') setClosedFor(undefined)
  }
  return (
    <section className="clu-update" aria-label="Update" data-update={status}>
      <div className="clu-update-head">
        <p className="clu-update-title">Update ready</p>
        <button type="button" className="clu-update-close" data-update-dismiss="" aria-label="Close" title="Close" onClick={() => void close()}>
          <svg width="10" height="10" viewBox="0 0 10 10" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" aria-hidden="true">
            <path d="M2 2l6 6M8 2l-6 6" />
          </svg>
        </button>
      </div>
      <p className="clu-update-detail">{version ? `Rio ${version} installs when you quit.` : 'It installs when you quit Rio.'}</p>
      <button type="button" className="clu-update-button" data-update-relaunch="" disabled={installing} onClick={() => void restart()}>
        Restart to update
      </button>
    </section>
  )
}
