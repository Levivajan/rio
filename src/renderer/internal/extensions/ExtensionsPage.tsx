// Extensions page (docs/plan/08-extensions.md F4): list, on and off, options, remove behind a confirm.
import { useCallback, useState } from 'react'
import type { ExtensionInfo } from '../../../shared/ipc/extensions'
import { Dialog } from '../../common/Dialog'
import { useAppearance } from '../../common/useReducedMotion'
import { useExtensionsPage } from './useExtensionsPage'
import '../internal.css'
import './extensions.css'
import { Button, Toggle } from '../../common/controls'

const STORE_URL = 'https://chromewebstore.google.com/'

export function ExtensionsPage() {
  useAppearance()
  const page = useExtensionsPage()
  const [confirming, setConfirming] = useState<ExtensionInfo | null>(null)
  const close = useCallback(() => setConfirming(null), [])
  return (
    <main className="clu-internal clu-extensions">
      <header className="clu-extensions-header">
        <h1>Extensions</h1>
        <a
          href={STORE_URL}
          onClick={(e) => {
            e.preventDefault()
            void page.openStore()
          }}
        >
          Get extensions
        </a>
      </header>
      {page.profiles.length > 1 && (
        <label className="clu-extensions-profile">
          <span>Profile</span>
          <select value={page.profileId ?? ''} onChange={(e) => page.pickProfile(e.target.value)}>
            {page.profiles.map((p) => (
              <option key={p.id} value={p.id}>
                {p.name}
              </option>
            ))}
          </select>
        </label>
      )}
      {page.error && (
        <p className="clu-extensions-error" role="alert">
          {page.error}
        </p>
      )}
      {page.extensions.length === 0 ? (
        <p className="clu-extensions-empty">No extensions in this profile yet.</p>
      ) : (
        <>
          <ul className="clu-extensions-list" aria-label="Installed extensions">
            {page.extensions.map((ext) => (
              <ExtensionRow
                key={ext.id}
                ext={ext}
                onToggle={(on) => void page.setEnabled(ext.id, on)}
                onOptions={() => void page.openOptions(ext.id)}
                onRemove={() => setConfirming(ext)}
              />
            ))}
          </ul>
          <p className="clu-extensions-note">Sleeping tabs are hidden from extensions until you open them.</p>
        </>
      )}
      {confirming && (
        <div className="clu-extensions-scrim">
          <Dialog title={`Remove “${confirming.name}”?`} onCancel={close}>
            <p className="clu-extensions-confirm-text">It is deleted from this profile. You can add it again from the Chrome Web Store.</p>
            <div className="clu-extensions-confirm-actions">
              <Button onClick={close}>
                Cancel
              </Button>
              <Button
                variant="danger"
                onClick={() => {
                  const { id } = confirming
                  close()
                  void page.remove(id)
                }}
              >
                Remove
              </Button>
            </div>
          </Dialog>
        </div>
      )}
    </main>
  )
}

function ExtensionRow({ ext, onToggle, onOptions, onRemove }: { ext: ExtensionInfo; onToggle: (on: boolean) => void; onOptions: () => void; onRemove: () => void }) {
  return (
    <li className="clu-extension" data-extension={ext.id} data-state={ext.enabled ? 'on' : 'off'}>
      {ext.icon ? <img className="clu-extension-icon" src={ext.icon} alt="" width={32} height={32} /> : <span className="clu-extension-icon" aria-hidden="true" />}
      <div className="clu-extension-text">
        <span className="clu-extension-title">
          <span className="clu-extension-name" title={ext.name}>
            {ext.name}
          </span>
          <span className="clu-extension-version">{ext.version}</span>
        </span>
        {ext.permissions.length > 0 && <span className="clu-extension-permissions">{ext.permissions.join(' · ')}</span>}
      </div>
      {ext.hasOptions && (
        <Button aria-label={`Options for ${ext.name}`} disabled={!ext.enabled} onClick={onOptions}>
          Options
        </Button>
      )}
      <Button aria-label={`Remove ${ext.name}`} onClick={onRemove}>
        Remove
      </Button>
      <Toggle label={`Turn on ${ext.name}`} checked={ext.enabled} onChange={onToggle} />
    </li>
  )
}
