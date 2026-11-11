// Profile manager (docs/plan/03-spaces.md "Manage profiles"): a New profile field, then every profile
// with Rename and Delete. Delete is disabled with its reason for the default profile and for profiles in use, and
// asks once inline before it clears the profile's site data.
import { useState } from 'react'
import { normalizeName } from '../../../shared/spaces'
import type { IpcResult } from '../../../shared/ipc'
import type { ProfileState } from '../../../shared/state'
import { Dialog } from '../../common/Dialog'
import { stateMirror } from '../../common/store'
import { sameItems, useCluState } from '../../common/useCluState'
import { profileDeleteBlock } from './editorModel'
import { clu } from '../../common/clu'
import { Button, Input } from '../../common/controls'

const failure = (r: Extract<IpcResult<unknown>, { ok: false }>) =>
  r.error.reason === 'max-profiles' ? 'Rio holds at most 20 profiles. Delete one to add another.' : 'The profile could not be changed.'

function ProfileRow({ profile, onError }: { profile: ProfileState; onError: (msg: string | null) => void }) {
  const block = useCluState((s) => profileDeleteBlock(s, profile.id))
  const [renaming, setRenaming] = useState<string | null>(null)
  const [confirming, setConfirming] = useState(false)
  const rename = async () => {
    const clean = normalizeName(renaming)
    setRenaming(null)
    if (!clean || clean === profile.name) return
    const r = await clu().profiles.rename({ profileId: profile.id, name: clean })
    onError(r.ok ? null : failure(r))
  }
  const remove = async () => {
    setConfirming(false)
    const r = await clu().profiles.delete({ profileId: profile.id })
    onError(r.ok ? null : failure(r))
  }
  return (
    <li className="clu-profile-row" data-profile-id={profile.id}>
      <div className="clu-profile-name">
        {renaming !== null ? (
          <Input
            aria-label={`New name for ${profile.name}`}
            value={renaming}
            maxLength={240}
            autoFocus
            spellCheck={false}
            onChange={(e) => setRenaming(e.currentTarget.value)}
            onBlur={() => void rename()}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && !e.nativeEvent.isComposing) {
                e.preventDefault()
                void rename()
              } else if (e.key === 'Escape') {
                // Esc ends the rename only, not the dialog.
                e.preventDefault()
                e.stopPropagation()
                setRenaming(null)
              }
            }}
          />
        ) : (
          <span>{profile.name}</span>
        )}
        <span className="clu-editor-hint">{confirming ? 'Its cookies and site data are removed.' : (block ?? 'Not used by any space')}</span>
      </div>
      {confirming ? (
        <>
          <Button onClick={() => setConfirming(false)}>
            Cancel
          </Button>
          <Button variant="danger" onClick={() => void remove()}>
            Delete
          </Button>
        </>
      ) : (
        <>
          <Button aria-label={`Rename ${profile.name}`} disabled={renaming !== null} onClick={() => setRenaming(profile.name)}>
            Rename
          </Button>
          <Button aria-label={`Delete ${profile.name}`} disabled={block !== null} onClick={() => setConfirming(true)}>
            Delete
          </Button>
        </>
      )}
    </li>
  )
}

export function ProfileManager({ onDone, doneLabel }: { onDone: () => void; doneLabel: string }) {
  const profiles = useCluState((s) => Object.values(s.profiles).filter((p) => !p.ephemeral), sameItems)
  const [name, setName] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const add = async () => {
    if (busy) return
    setBusy(true)
    const clean = normalizeName(name)
    const r = await clu().profiles.create(clean ? { name: clean } : {})
    setBusy(false)
    if (!r.ok) return setError(failure(r))
    setError(null)
    setName('')
  }
  const placeholder = `Profile ${Object.values(stateMirror().get().profiles).filter((p) => !p.ephemeral).length + 1}`
  return (
    <Dialog title="Profiles" onCancel={onDone}>
      <p className="clu-editor-body">Each profile keeps its own logins, cookies, history and extensions. Spaces on the same profile share them.</p>
      <form
        className="clu-editor-row"
        onSubmit={(e) => {
          e.preventDefault()
          void add()
        }}
      >
        <Input aria-label="New profile name" placeholder={placeholder} value={name} maxLength={240} spellCheck={false} onChange={(e) => setName(e.currentTarget.value)} />
        <Button type="submit" disabled={busy}>
          Add Profile
        </Button>
      </form>
      <ul className="clu-profile-list" aria-label="Profiles">
        {profiles.map((p) => (
          <ProfileRow key={p.id} profile={p} onError={setError} />
        ))}
      </ul>
      {error ? (
        <p className="clu-editor-error" role="alert">
          {error}
        </p>
      ) : null}
      <div className="clu-editor-footer">
        <span />
        <Button variant="primary" onClick={onDone}>
          {doneLabel}
        </Button>
      </div>
    </Dialog>
  )
}
