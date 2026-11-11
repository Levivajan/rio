// New Space as a panel over the sidebar:
// an illustration, the title and one line of text, a name field with an icon well, the Profile row, Choose a Theme,
// then Create Space (dim until a name is typed) and Cancel at the bottom. The top row and the bottom bar stay visible
// around it and the page is not dimmed. Enter creates, Esc cancels.
import { useState } from 'react'
import { SPACE_COLOR_HEX, SPACE_COLORS, type SpaceIcon } from '../../../shared/spaces'
import type { ProfileState } from '../../../shared/state'
import { sameItems, useCluState } from '../../common/useCluState'
import { SpaceSymbolGlyph } from '../../common/spaceSymbols'
import { NEW_PROFILE, saveDraft, type Draft } from './editorModel'
import { IconPicker } from './IconPicker'
import { clu } from '../../common/clu'

const colorName = (c: string) => c.charAt(0).toUpperCase() + c.slice(1)
const liveProfiles = (all: Record<string, ProfileState>) => Object.values(all).filter((p) => !p.ephemeral)

/** The panel's place over the sidebar: its left edge and width. */
function useSidebarBox(windowId: string): { left: number; width: number } {
  return useCluState(
    (s) => {
      const shell = s.windows[windowId]?.shell
      const r = shell?.layout.sidebarRect
      return r && r.width > 0 ? { left: r.x, width: r.width } : { left: 0, width: shell?.sidebar.width ?? 0 }
    },
    (a, b) => a.left === b.left && a.width === b.width,
  )
}

/** Three tilted cards: separate stacks of tabs. */
function Illustration() {
  return (
    <svg className="clu-create-art" width="68" height="48" viewBox="0 0 68 48" aria-hidden="true">
      <g transform="rotate(-10 18 26)">
        <rect x="4" y="8" width="28" height="36" rx="4" fill="#fff" />
        <rect x="8" y="13" width="20" height="14" rx="2" fill="#7fb77e" />
      </g>
      <g transform="rotate(10 50 26)">
        <rect x="36" y="8" width="28" height="36" rx="4" fill="#fff" />
        <rect x="40" y="13" width="20" height="14" rx="2" fill="#e58f7b" />
      </g>
      <rect x="20" y="3" width="28" height="38" rx="4" fill="#fff" stroke="rgba(0,0,0,0.12)" />
      <rect x="24" y="8" width="20" height="15" rx="2" fill="#5b5f73" />
      <rect x="24" y="27" width="14" height="2.5" rx="1.25" fill="#c9cbd6" />
      <rect x="24" y="32" width="10" height="2.5" rx="1.25" fill="#c9cbd6" />
    </svg>
  )
}

function IconWell({ icon, open, onToggle }: { icon: SpaceIcon; open: boolean; onToggle: () => void }) {
  return (
    <button type="button" className="clu-create-well" aria-label="Space icon" aria-expanded={open} data-empty={icon.kind === 'letter' || undefined} onClick={onToggle}>
      {icon.kind === 'emoji' ? (
        icon.value
      ) : icon.kind === 'symbol' ? (
        <SpaceSymbolGlyph symbol={icon.value} size={14} />
      ) : (
        <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true">
          <path d="M5 1v8M1 5h8" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
        </svg>
      )}
    </button>
  )
}

export function CreateSpacePanel({ windowId, draft, setDraft, onClose }: { windowId: string; draft: Draft; setDraft: (d: Draft) => void; onClose: () => void }) {
  const box = useSidebarBox(windowId)
  const profiles = useCluState((s) => liveProfiles(s.profiles), sameItems)
  const [icons, setIcons] = useState(false)
  const [themes, setThemes] = useState(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const set = (patch: Partial<Draft>) => setDraft({ ...draft, ...patch })
  // A profile deleted elsewhere while the panel is open falls back to the first one.
  const profile = draft.profile === NEW_PROFILE || profiles.some((p) => p.id === draft.profile) ? draft.profile : (profiles[0]?.id ?? NEW_PROFILE)
  const named = draft.name.trim() !== '' && (profile !== NEW_PROFILE || draft.newProfileName.trim() !== '')

  const create = async () => {
    if (busy || !named) return
    setBusy(true)
    setError(null)
    const r = await saveDraft(clu(), { ...draft, profile }, { space: null, tabCount: 0, confirmed: false })
    setBusy(false)
    if (r.status === 'done') onClose()
    else if (r.status === 'error') setError(r.message)
  }

  return (
    <form
      role="dialog"
      aria-label="Create a Space"
      className="clu-create-space"
      style={{ left: box.left, width: box.width }}
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={(e) => {
        if (e.key !== 'Escape') return
        e.preventDefault()
        onClose()
      }}
      onSubmit={(e) => {
        e.preventDefault()
        void create()
      }}
    >
      <div className="clu-create-head">
        <Illustration />
        <h2 className="clu-create-title">Create a Space</h2>
        <p className="clu-create-text">Give work, home and each project their own tabs.</p>
      </div>
      <div className="clu-create-rows">
        <div className="clu-create-row clu-create-name">
          <IconWell icon={draft.icon} open={icons} onToggle={() => setIcons(!icons)} />
          <input aria-label="Space name" placeholder="Space name…" value={draft.name} maxLength={240} spellCheck={false} autoFocus onChange={(e) => set({ name: e.currentTarget.value })} />
        </div>
        {icons ? <IconPicker icon={draft.icon} name={draft.name} color={draft.color} onPick={(icon) => set({ icon })} /> : null}
        <div className="clu-create-row clu-create-profile">
          <label htmlFor="clu-create-profile">Profile</label>
          <select id="clu-create-profile" value={profile} onChange={(e) => set({ profile: e.currentTarget.value })}>
            {profiles.map((p) => (
              <option key={p.id} value={p.id}>
                {p.name}
              </option>
            ))}
            <option value={NEW_PROFILE}>New profile…</option>
          </select>
          <span className="clu-create-info" role="img" aria-label="About profiles" title="A space's tabs use its profile's logins, history and extensions.">
            <svg width="14" height="14" viewBox="0 0 14 14" aria-hidden="true">
              <circle cx="7" cy="7" r="6" fill="none" stroke="currentColor" strokeWidth="1.1" />
              <path d="M7 6.2v3.6" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
              <circle cx="7" cy="4.3" r="0.75" fill="currentColor" />
            </svg>
          </span>
        </div>
        {profile === NEW_PROFILE ? (
          <div className="clu-create-row">
            <input aria-label="New profile name" placeholder="Profile name" value={draft.newProfileName} maxLength={240} spellCheck={false} onChange={(e) => set({ newProfileName: e.currentTarget.value })} />
          </div>
        ) : null}
        <button type="button" className="clu-create-row clu-create-theme" aria-expanded={themes} onClick={() => setThemes(!themes)}>
          Choose a Theme
          {themes ? null : <span className="clu-create-swatch" style={{ background: SPACE_COLOR_HEX[draft.color] }} />}
        </button>
        {themes ? (
          <div className="clu-create-colors" role="group" aria-label="Themes">
            {SPACE_COLORS.map((c) => (
              <button key={c} type="button" className="clu-create-color" aria-label={colorName(c)} aria-pressed={draft.color === c} style={{ background: SPACE_COLOR_HEX[c] }} onClick={() => set({ color: c })} />
            ))}
          </div>
        ) : null}
        {error ? (
          <p className="clu-create-error" role="alert">
            {error}
          </p>
        ) : null}
      </div>
      <div className="clu-create-actions">
        <button type="submit" className="clu-create-submit" disabled={busy || !named}>
          Create Space
        </button>
        <button type="button" className="clu-create-cancel" onClick={onClose}>
          Cancel
        </button>
      </div>
    </form>
  )
}
