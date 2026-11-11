// Space editor logic (docs/plan/03-spaces.md "Create and edit sheet"): the draft, emoji input and the
// save sequence over an injected bridge, so it is unit tested without a window. Main validates everything again.
import { NEW_SPACE_COLOR, nextDefaultName, normalizeName, parseEmoji, type SpaceColor, type SpaceIcon } from '../../../shared/spaces'
import type { IpcResult } from '../../../shared/ipc'
import type { CluState, SpaceState } from '../../../shared/state'

/** 64 curated emoji for the icon grid (Clu's own set). Any other emoji can be pasted into the field. */
export const CURATED_EMOJI = [
  '💼', '🏠', '💻', '📚', '🎨', '🎮', '🎵', '🎬',
  '📷', '✈️', '🌍', '🛒', '💡', '🔬', '🧪', '📱',
  '📊', '📈', '📝', '📅', '📌', '🔖', '🗂️', '📦',
  '🔧', '🛠️', '⚙️', '🚀', '⭐', '🔥', '⚡', '🌙',
  '☀️', '🌈', '🌱', '🌸', '🍀', '🍕', '☕', '🍎',
  '🏃', '🧘', '⚽', '🏀', '🎯', '🏆', '🎁', '🎉',
  '💬', '📣', '❤️', '💰', '🏦', '🏥', '🎓', '👶',
  '🐶', '🐱', '🦊', '🐝', '🔒', '🧠', '🗞️', '🧩'
] as const

export const NEW_PROFILE = 'new'

export interface Draft {
  name: string
  icon: SpaceIcon
  color: SpaceColor
  /** A profile id, or NEW_PROFILE for the inline "New profile…" name field. */
  profile: string
  newProfileName: string
}

/** The editor's starting values: the space's own, or `Space N`, the letter icon, lavender
 * (indigo) and the default profile. */
export function initialDraft(s: Pick<CluState, 'spaces' | 'spaceOrder' | 'profiles'>, spaceId: string | null): Draft {
  const space = spaceId ? s.spaces[spaceId] : undefined
  if (space) return { name: space.name, icon: space.icon, color: space.color, profile: space.profileId, newProfileName: '' }
  const names = s.spaceOrder.map((id) => s.spaces[id]?.name ?? '')
  const profiles = Object.values(s.profiles).filter((p) => !p.ephemeral)
  const profile = (profiles.find((p) => p.isDefault) ?? profiles[0])?.id ?? NEW_PROFILE
  return { name: nextDefaultName('Space', s.spaceOrder.length + 1, names), icon: { kind: 'letter' }, color: NEW_SPACE_COLOR, profile, newProfileName: '' }
}

/** Text typed or pasted into the emoji field: an icon when it is exactly one emoji, an error otherwise, null when empty. */
export function emojiInput(raw: string): { icon: SpaceIcon } | { error: string } | null {
  if (!raw.trim()) return null
  const value = parseEmoji(raw)
  return value ? { icon: { kind: 'emoji', value } } : { error: 'Use a single emoji' }
}

export const sameIcon = (a: SpaceIcon, b: SpaceIcon) => a.kind === b.kind && (a.kind === 'letter' || (b.kind !== 'letter' && a.value === b.value))

/** The bridge calls a save makes (a subset of window.clu). */
export interface EditorApi {
  spaces: {
    create(p: { name?: string; icon?: SpaceIcon; color?: SpaceColor; profileId?: string }): Promise<IpcResult<{ spaceId: string }>>
    update(p: { spaceId: string; name?: string; icon?: SpaceIcon; color?: SpaceColor }): Promise<IpcResult<void>>
    setProfile(p: { spaceId: string; profileId: string; confirmed?: boolean }): Promise<IpcResult<{ status: 'done' } | { status: 'needs-confirmation'; tabCount: number }>>
  }
  profiles: {
    create(p: { name?: string }): Promise<IpcResult<{ profileId: string }>>
    delete(p: { profileId: string }): Promise<IpcResult<void>>
  }
}

export type SaveResult = { status: 'done' } | { status: 'confirm'; tabCount: number } | { status: 'error'; message: string }

function messageFor(r: Extract<IpcResult<unknown>, { ok: false }>): string {
  if (r.error.reason === 'max-spaces') return 'Rio holds at most 50 spaces. Delete one to add another.'
  if (r.error.reason === 'max-profiles') return 'Rio holds at most 20 profiles. Delete one to add another.'
  if (r.error.code === 'not-found') return 'This space or profile no longer exists.'
  return 'The space could not be saved.'
}

/**
 * Saves the draft. An existing space with tabs that changes profile returns `confirm` first and writes nothing;
 * call again with `confirmed` after the user agrees. A new profile is created only once the save goes ahead.
 */
export async function saveDraft(api: EditorApi, draft: Draft, opts: { space: SpaceState | null; tabCount: number; confirmed: boolean }): Promise<SaveResult> {
  const { space } = opts
  const name = normalizeName(draft.name)
  if (space && !name) return { status: 'error', message: 'Enter a name for the space.' }
  const changesProfile = draft.profile === NEW_PROFILE || (space !== null && draft.profile !== space.profileId)
  if (space && changesProfile && opts.tabCount > 0 && !opts.confirmed) return { status: 'confirm', tabCount: opts.tabCount }
  // A space of another profile shows no tabs in this window's state (each profile keeps its rows to itself), so main
  // counts them: setProfile without `confirmed` answers needs-confirmation when the space has any, and writes nothing.
  let profileMoved = false
  if (space && changesProfile && draft.profile !== NEW_PROFILE && !opts.confirmed) {
    const r = await api.spaces.setProfile({ spaceId: space.id, profileId: draft.profile })
    if (!r.ok) return { status: 'error', message: messageFor(r) }
    if (r.value.status === 'needs-confirmation') return { status: 'confirm', tabCount: r.value.tabCount }
    profileMoved = true
  }

  let profileId = draft.profile
  let madeProfile = false
  if (draft.profile === NEW_PROFILE) {
    const clean = normalizeName(draft.newProfileName)
    const r = await api.profiles.create(clean ? { name: clean } : {})
    if (!r.ok) return { status: 'error', message: messageFor(r) }
    profileId = r.value.profileId
    madeProfile = true
  }
  /** A save that fails after making its new profile removes it, so a retry does not leave "Profile N" behind. */
  const dropMadeProfile = async () => {
    if (madeProfile) await api.profiles.delete({ profileId }).catch(() => undefined)
  }
  const failed = async (r: Extract<IpcResult<unknown>, { ok: false }>): Promise<SaveResult> => {
    await dropMadeProfile()
    return { status: 'error', message: messageFor(r) }
  }
  // The same count for a space moving to the profile just made: its tabs may be rows this window cannot see. On
  // needs-confirmation the new profile goes again, and the confirmed retry makes it anew.
  if (space && madeProfile && !opts.confirmed) {
    const r = await api.spaces.setProfile({ spaceId: space.id, profileId })
    if (!r.ok) return failed(r)
    if (r.value.status === 'needs-confirmation') {
      await dropMadeProfile()
      return { status: 'confirm', tabCount: r.value.tabCount }
    }
    profileMoved = true
  }

  if (!space) {
    const r = await api.spaces.create({ ...(name ? { name } : {}), icon: draft.icon, color: draft.color, profileId })
    return r.ok ? { status: 'done' } : failed(r)
  }
  if (name !== space.name || draft.color !== space.color || !sameIcon(draft.icon, space.icon)) {
    const r = await api.spaces.update({ spaceId: space.id, ...(name !== space.name && name ? { name } : {}), icon: draft.icon, color: draft.color })
    if (!r.ok) return { status: 'error', message: messageFor(r) }
  }
  if (profileId !== space.profileId && !profileMoved) {
    const r = await api.spaces.setProfile({ spaceId: space.id, profileId, confirmed: true })
    if (!r.ok) return failed(r)
  }
  return { status: 'done' }
}

/** Why a profile cannot be deleted, or null when it can. */
export function profileDeleteBlock(s: Pick<CluState, 'spaces' | 'profiles'>, profileId: string): string | null {
  const p = s.profiles[profileId]
  if (!p) return 'This profile no longer exists.'
  if (p.isDefault) return 'The default profile stays.'
  const used = Object.values(s.spaces).filter((sp) => sp.profileId === profileId && !sp.ephemeral).length
  if (used > 0) return `Used by ${used} ${used === 1 ? 'space' : 'spaces'}.`
  return null
}
