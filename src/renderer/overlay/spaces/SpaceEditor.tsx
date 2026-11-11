// Space editor surface (docs/plan/03-spaces.md "Create and edit sheet"): name, icon grid (Emoji with a paste
// field, or Symbols with a search field), 12 color swatches, profile dropdown with an inline New profile… field,
// Theme… and the profile manager. A new space (no spaceId) gets the Create a Space panel over the sidebar instead
// (CreateSpacePanel.tsx).
// Enter saves and Esc closes without changes. Changing an existing space's profile asks first when it has tabs.
import { useEffect, useRef, useState } from 'react'
import { SPACE_COLOR_HEX, SPACE_COLORS, type SpaceEditorProps } from '../../../shared/spaces'
import type { ProfileState } from '../../../shared/state'
import { Dialog } from '../../common/Dialog'
import { invoke } from '../../common/invoke'
import { stateMirror } from '../../common/store'
import { sameItems, useCluState } from '../../common/useCluState'
import { closeInstance, type SurfaceProps } from '../core/types'
import { initialDraft, NEW_PROFILE, saveDraft, type Draft } from './editorModel'
import { ProfileManager } from './ProfileManager'
import { CreateSpacePanel } from './CreateSpacePanel'
import { IconPicker } from './IconPicker'
import './editor.css'
import { clu } from '../../common/clu'
import { Button, Input, Select } from '../../common/controls'

/** theme.openPicker answered not-implemented this run, so Theme… renders disabled from then on. */
let themePickerMissing = false

const colorName = (c: string) => c.charAt(0).toUpperCase() + c.slice(1)
const liveProfiles = (all: Record<string, ProfileState>) => Object.values(all).filter((p) => !p.ephemeral)

function ThemeButton({ spaceId }: { spaceId: string }) {
  const [missing, setMissing] = useState(themePickerMissing)
  return (
    <Button
      disabled={missing}
      onClick={async (e) => {
        const button = e.currentTarget
        const r = button.getBoundingClientRect()
        const anchor = { x: Math.round(r.x), y: Math.round(r.y), width: Math.round(r.width), height: Math.round(r.height) }
        const res = await invoke(clu().theme.openPicker({ spaceId, anchor }))
        if (!res.ok && res.disabled) {
          themePickerMissing = true
          // A disabled button drops focus to the body, where Esc and Tab no longer reach the dialog.
          button.closest('[role="dialog"]')?.querySelector<HTMLElement>('input')?.focus()
          setMissing(true)
        }
      }}
    >
      Theme…
    </Button>
  )
}

function SpaceForm({ spaceId, draft, setDraft, onClose, onManageProfiles }: { spaceId: string | null; draft: Draft; setDraft: (d: Draft) => void; onClose: () => void; onManageProfiles: () => void }) {
  const space = useCluState((s) => (spaceId ? s.spaces[spaceId] : undefined))
  const profiles = useCluState((s) => liveProfiles(s.profiles), sameItems)
  const tabCount = useCluState((s) => (spaceId ? Object.values(s.tabs).filter((t) => t.spaceId === spaceId).length : 0))
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [confirm, setConfirm] = useState<number | null>(null)
  const newProfileRef = useRef<HTMLInputElement>(null)
  const set = (patch: Partial<Draft>) => setDraft({ ...draft, ...patch })
  // A profile deleted elsewhere while the editor is open falls back to the first one.
  const profile = draft.profile === NEW_PROFILE || profiles.some((p) => p.id === draft.profile) ? draft.profile : (profiles[0]?.id ?? NEW_PROFILE)

  useEffect(() => {
    if (spaceId && !space) onClose()
  }, [spaceId, space, onClose])
  useEffect(() => {
    if (profile === NEW_PROFILE && document.activeElement?.tagName === 'SELECT') newProfileRef.current?.focus()
  }, [profile])

  const save = async (confirmed: boolean) => {
    if (busy) return
    setBusy(true)
    setError(null)
    const r = await saveDraft(clu(), { ...draft, profile }, { space: space ?? null, tabCount, confirmed })
    setBusy(false)
    if (r.status === 'done') onClose()
    else if (r.status === 'confirm') setConfirm(r.tabCount)
    else setError(r.message)
  }

  if (confirm !== null && space) {
    const target = profile === NEW_PROFILE ? draft.newProfileName.trim() || 'a new profile' : (profiles.find((p) => p.id === profile)?.name ?? 'another profile')
    const quoted = profile === NEW_PROFILE && !draft.newProfileName.trim() ? target : `“${target}”`
    return (
      <Dialog key="confirm" title={`Switch “${space.name}” to profile ${quoted}?`} onCancel={() => setConfirm(null)}>
        <p className="clu-editor-body">
          Its {confirm} {confirm === 1 ? 'tab' : 'tabs'} will reload and you may need to sign in again.
        </p>
        <div className="clu-editor-footer">
          <span />
          <Button onClick={() => setConfirm(null)}>
            Cancel
          </Button>
          <Button variant="primary" disabled={busy} onClick={() => void save(true)}>
            Switch Profile
          </Button>
        </div>
      </Dialog>
    )
  }

  return (
    <Dialog key="form" title={space ? 'Edit Space' : 'New Space'} onCancel={onClose}>
      <form
        className="clu-editor-form"
        onSubmit={(e) => {
          e.preventDefault()
          void save(false)
        }}
      >
        <label className="clu-editor-section">
          <span className="clu-editor-label">Name</span>
          <Input value={draft.name} maxLength={240} spellCheck={false} onFocus={(e) => e.currentTarget.select()} onChange={(e) => set({ name: e.currentTarget.value })} />
        </label>
        <IconPicker icon={draft.icon} name={draft.name} color={draft.color} onPick={(icon) => set({ icon })} />
        <div className="clu-editor-section">
          <span className="clu-editor-label" id="clu-editor-color">
            Color
          </span>
          <div className="clu-editor-colors" role="group" aria-labelledby="clu-editor-color">
            {SPACE_COLORS.map((c) => (
              <button key={c} type="button" className="clu-editor-swatch" aria-label={colorName(c)} aria-pressed={draft.color === c} data-color={c} style={{ background: SPACE_COLOR_HEX[c] }} onClick={() => set({ color: c })} />
            ))}
          </div>
        </div>
        <div className="clu-editor-section">
          <label className="clu-editor-label" htmlFor="clu-editor-profile">
            Profile
          </label>
          <div className="clu-editor-row">
            <Select id="clu-editor-profile" value={profile} onChange={(e) => set({ profile: e.currentTarget.value })}>
              {profiles.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.name}
                </option>
              ))}
              <option value={NEW_PROFILE}>New profile…</option>
            </Select>
            <Button onClick={onManageProfiles}>
              Manage…
            </Button>
          </div>
          {profile === NEW_PROFILE ? (
            <Input
              ref={newProfileRef}
              aria-label="New profile name"
              placeholder="Profile name"
              value={draft.newProfileName}
              maxLength={240}
              spellCheck={false}
              onChange={(e) => set({ newProfileName: e.currentTarget.value })}
            />
          ) : null}
        </div>
        {error ? (
          <p className="clu-editor-error" role="alert">
            {error}
          </p>
        ) : null}
        <div className="clu-editor-footer">
          {space ? <ThemeButton spaceId={space.id} /> : <span />}
          <Button onClick={onClose}>
            Cancel
          </Button>
          <Button type="submit" variant="primary" disabled={busy}>
            {space ? 'Save' : 'Create Space'}
          </Button>
        </div>
      </form>
    </Dialog>
  )
}

export function SpaceEditorSurface({ instance, windowId }: SurfaceProps) {
  const props = (instance.props ?? {}) as Partial<SpaceEditorProps>
  const spaceId = typeof props.spaceId === 'string' ? props.spaceId : null
  const fromProfiles = props.view === 'profiles'
  const [view, setView] = useState<'space' | 'profiles'>(fromProfiles ? 'profiles' : 'space')
  // A new space starts unnamed in the sidebar panel; Create Space stays dim until a name is typed.
  const [draft, setDraft] = useState(() => {
    const d = initialDraft(stateMirror().get(), spaceId)
    return spaceId ? d : { ...d, name: '' }
  })
  const [close] = useState(() => () => closeInstance(instance.instanceId))
  if (!spaceId && view === 'space') return <CreateSpacePanel windowId={windowId} draft={draft} setDraft={setDraft} onClose={close} />
  return (
    <div className="clu-center clu-space-editor" data-view={view} onPointerDown={(e) => e.stopPropagation()}>
      {view === 'space' ? (
        <SpaceForm spaceId={spaceId} draft={draft} setDraft={setDraft} onClose={close} onManageProfiles={() => setView('profiles')} />
      ) : (
        <ProfileManager onDone={fromProfiles ? close : () => setView('space')} doneLabel={fromProfiles ? 'Done' : 'Back'} />
      )}
    </div>
  )
}
