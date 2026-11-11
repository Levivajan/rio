import { describe, expect, it } from 'vitest'
import { parseEmoji } from '../../../shared/spaces'
import type { IpcResult } from '../../../shared/ipc'
import { defaultProfileState, defaultSpaceState, defaultState, type CluState } from '../../../shared/state'
import { CURATED_EMOJI, emojiInput, initialDraft, NEW_PROFILE, profileDeleteBlock, saveDraft, type EditorApi } from './editorModel'

const DEF = '11111111-1111-4111-8111-111111111111'
const CLIENT = '22222222-2222-4222-8222-222222222222'
const HOME = '33333333-3333-4333-8333-333333333333'
const NEW_ID = '44444444-4444-4444-8444-444444444444'

function world(): CluState {
  const s = defaultState()
  s.profiles[DEF] = defaultProfileState({ id: DEF, name: 'Default', isDefault: true, ephemeral: false })
  s.profiles[CLIENT] = defaultProfileState({ id: CLIENT, name: 'Client', isDefault: false, ephemeral: false })
  s.spaces[HOME] = defaultSpaceState({ id: HOME, name: 'Home', icon: { kind: 'letter' }, color: 'blue', profileId: DEF, ephemeral: false })
  s.spaceOrder = [HOME]
  return s
}

type SetProfileAnswer = { status: 'done' } | { status: 'needs-confirmation'; tabCount: number }

function fakeApi(fail: Partial<Record<string, { code: string; reason?: string }>> = {}, setProfile: SetProfileAnswer = { status: 'done' }) {
  const calls: [string, unknown][] = []
  const answer = <T>(name: string, value: T): Promise<IpcResult<T>> => {
    const f = fail[name]
    return Promise.resolve(f ? ({ ok: false, error: f } as IpcResult<T>) : { ok: true, value })
  }
  const api: EditorApi = {
    spaces: {
      create: (p) => (calls.push(['spaces.create', p]), answer('spaces.create', { spaceId: NEW_ID })),
      update: (p) => (calls.push(['spaces.update', p]), answer('spaces.update', undefined)),
      setProfile: (p) => (calls.push(['spaces.setProfile', p]), answer<SetProfileAnswer>('spaces.setProfile', p.confirmed ? { status: 'done' } : setProfile))
    },
    profiles: {
      create: (p) => (calls.push(['profiles.create', p]), answer('profiles.create', { profileId: CLIENT })),
      delete: (p) => (calls.push(['profiles.delete', p]), answer('profiles.delete', undefined))
    }
  }
  return { api, calls: () => calls }
}

describe('space editor model', () => {
  it('ships 64 curated emoji, each one valid and unique', () => {
    expect(CURATED_EMOJI).toHaveLength(64)
    expect(new Set(CURATED_EMOJI).size).toBe(64)
    for (const e of CURATED_EMOJI) expect(parseEmoji(e), e).toBe(e)
  })

  it('starts a new space as the next Space N on the default profile, and an edit from the space itself', () => {
    const s = world()
    expect(initialDraft(s, null)).toEqual({ name: 'Space 2', icon: { kind: 'letter' }, color: 'indigo', profile: DEF, newProfileName: '' })
    expect(initialDraft(s, HOME)).toMatchObject({ name: 'Home', profile: DEF })
    // An unknown id is a new space.
    expect(initialDraft(s, NEW_ID).name).toBe('Space 2')
  })

  it('accepts exactly one emoji from the field and reports anything else', () => {
    expect(emojiInput('💼')).toEqual({ icon: { kind: 'emoji', value: '💼' } })
    expect(emojiInput(' 👩‍💻 ')).toEqual({ icon: { kind: 'emoji', value: '👩‍💻' } })
    expect(emojiInput('abc')).toEqual({ error: 'Use a single emoji' })
    expect(emojiInput('💼💼')).toEqual({ error: 'Use a single emoji' })
    expect(emojiInput('<script>')).toEqual({ error: 'Use a single emoji' })
    expect(emojiInput('   ')).toBeNull()
  })

  it('creates a new profile first, then the space on it', async () => {
    const { api, calls } = fakeApi()
    const draft = { name: ' Work ', icon: { kind: 'emoji', value: '💼' } as const, color: 'teal' as const, profile: NEW_PROFILE, newProfileName: 'Client' }
    expect(await saveDraft(api, draft, { space: null, tabCount: 0, confirmed: false })).toEqual({ status: 'done' })
    expect(calls()).toEqual([
      ['profiles.create', { name: 'Client' }],
      ['spaces.create', { name: 'Work', icon: { kind: 'emoji', value: '💼' }, color: 'teal', profileId: CLIENT }]
    ])
  })

  it('removes the profile it just made when the space cannot be created', async () => {
    const { api, calls } = fakeApi({ 'spaces.create': { code: 'rejected', reason: 'max-spaces' } })
    const draft = { name: 'Work', icon: { kind: 'letter' } as const, color: 'teal' as const, profile: NEW_PROFILE, newProfileName: 'Client' }
    const r = await saveDraft(api, draft, { space: null, tabCount: 0, confirmed: false })
    expect(r).toEqual({ status: 'error', message: 'Rio holds at most 50 spaces. Delete one to add another.' })
    expect(calls().map(([n]) => n)).toEqual(['profiles.create', 'spaces.create', 'profiles.delete'])
    expect(calls().at(-1)).toEqual(['profiles.delete', { profileId: CLIENT }])
  })

  it('lets main pick the default name for an empty one, and reports the space cap', async () => {
    const { api, calls } = fakeApi({ 'spaces.create': { code: 'rejected', reason: 'max-spaces' } })
    const r = await saveDraft(api, { ...initialDraft(world(), null), name: '  ' }, { space: null, tabCount: 0, confirmed: false })
    expect(r).toEqual({ status: 'error', message: 'Rio holds at most 50 spaces. Delete one to add another.' })
    expect(calls()).toEqual([['spaces.create', { icon: { kind: 'letter' }, color: 'indigo', profileId: DEF }]])
  })

  it('asks before moving a space with tabs to another profile, writing nothing until confirmed', async () => {
    const s = world()
    const space = s.spaces[HOME] ?? null
    const draft = { ...initialDraft(s, HOME), name: 'Personal', profile: CLIENT }
    const first = fakeApi()
    expect(await saveDraft(first.api, draft, { space, tabCount: 3, confirmed: false })).toEqual({ status: 'confirm', tabCount: 3 })
    expect(first.calls()).toEqual([])
    const second = fakeApi()
    expect(await saveDraft(second.api, draft, { space, tabCount: 3, confirmed: true })).toEqual({ status: 'done' })
    expect(second.calls()).toEqual([
      ['spaces.update', { spaceId: HOME, name: 'Personal', icon: { kind: 'letter' }, color: 'blue' }],
      ['spaces.setProfile', { spaceId: HOME, profileId: CLIENT, confirmed: true }]
    ])
  })

  it('lets main count the tabs of a space whose rows this window cannot see', async () => {
    const s = world()
    const space = s.spaces[HOME] ?? null
    const draft = { ...initialDraft(s, HOME), name: 'Personal', profile: CLIENT }
    // No tabs in this window's state, but main holds 2: it asks, and nothing else is written.
    const asking = fakeApi({}, { status: 'needs-confirmation', tabCount: 2 })
    expect(await saveDraft(asking.api, draft, { space, tabCount: 0, confirmed: false })).toEqual({ status: 'confirm', tabCount: 2 })
    expect(asking.calls()).toEqual([['spaces.setProfile', { spaceId: HOME, profileId: CLIENT }]])
    // An empty space moves on the first call, and the profile is not set twice.
    const empty = fakeApi()
    expect(await saveDraft(empty.api, draft, { space, tabCount: 0, confirmed: false })).toEqual({ status: 'done' })
    expect(empty.calls()).toEqual([
      ['spaces.setProfile', { spaceId: HOME, profileId: CLIENT }],
      ['spaces.update', { spaceId: HOME, name: 'Personal', icon: { kind: 'letter' }, color: 'blue' }]
    ])
    const refused = fakeApi({ 'spaces.setProfile': { code: 'not-found' } })
    expect(await saveDraft(refused.api, draft, { space, tabCount: 0, confirmed: false })).toEqual({ status: 'error', message: 'This space or profile no longer exists.' })
  })

  it('asks main before moving another profile\'s space with tabs to a new profile', async () => {
    const s = world()
    const space = s.spaces[HOME] ?? null
    const draft = { ...initialDraft(s, HOME), profile: NEW_PROFILE, newProfileName: 'Fresh' }
    // This window sees none of the space's tabs; main holds 5. Nothing moves, and the profile made to ask goes again.
    const asking = fakeApi({}, { status: 'needs-confirmation', tabCount: 5 })
    expect(await saveDraft(asking.api, draft, { space, tabCount: 0, confirmed: false })).toEqual({ status: 'confirm', tabCount: 5 })
    expect(asking.calls()).toEqual([
      ['profiles.create', { name: 'Fresh' }],
      ['spaces.setProfile', { spaceId: HOME, profileId: CLIENT }],
      ['profiles.delete', { profileId: CLIENT }]
    ])
    // Confirmed: the profile is made and the space moves with confirmed set.
    const confirmed = fakeApi()
    expect(await saveDraft(confirmed.api, draft, { space, tabCount: 0, confirmed: true })).toEqual({ status: 'done' })
    expect(confirmed.calls()).toEqual([
      ['profiles.create', { name: 'Fresh' }],
      ['spaces.setProfile', { spaceId: HOME, profileId: CLIENT, confirmed: true }]
    ])
  })

  it('saves nothing for an unchanged edit and refuses an empty name', async () => {
    const s = world()
    const space = s.spaces[HOME] ?? null
    const { api, calls } = fakeApi()
    expect(await saveDraft(api, initialDraft(s, HOME), { space, tabCount: 2, confirmed: false })).toEqual({ status: 'done' })
    expect(await saveDraft(api, { ...initialDraft(s, HOME), name: '\n' }, { space, tabCount: 0, confirmed: false })).toEqual({ status: 'error', message: 'Enter a name for the space.' })
    expect(calls()).toEqual([])
  })

  it('stops when the new profile cannot be created', async () => {
    const { api, calls } = fakeApi({ 'profiles.create': { code: 'rejected', reason: 'max-profiles' } })
    const r = await saveDraft(api, { ...initialDraft(world(), null), profile: NEW_PROFILE }, { space: null, tabCount: 0, confirmed: false })
    expect(r).toEqual({ status: 'error', message: 'Rio holds at most 20 profiles. Delete one to add another.' })
    expect(calls()).toEqual([['profiles.create', {}]])
  })

  it('gives the reason a profile cannot be deleted', () => {
    const s = world()
    expect(profileDeleteBlock(s, DEF)).toBe('The default profile stays.')
    expect(profileDeleteBlock(s, CLIENT)).toBeNull()
    s.spaces[HOME] = { ...(s.spaces[HOME] as NonNullable<CluState['spaces'][string]>), profileId: CLIENT }
    expect(profileDeleteBlock(s, CLIENT)).toBe('Used by 1 space.')
    expect(profileDeleteBlock(s, NEW_ID)).toBe('This profile no longer exists.')
  })
})
