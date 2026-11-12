// A patch to one tab's content re-renders that row only and forces no layout read; a change that can move rows (a
// subtitle that grows a row) still runs the row slide's layout pass.
import { act, render } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { defaultSpaceState, defaultState, defaultTabState, defaultWindowState, type PatchOp } from '../../../shared/state'
import { stubClu } from '../../../test-utils/stubClu'
import * as rowModel from './rowModel'
import { SpaceTabs } from './SpaceTabs'

vi.mock('./rowModel', async (load) => {
  const m = await load<typeof import('./rowModel')>()
  return { ...m, rowView: vi.fn(m.rowView) }
})

const WIN = '00000000-0000-4000-8000-000000000001'
const SPACE = '00000000-0000-4000-8000-000000000002'
const PROFILE = '00000000-0000-4000-8000-000000000003'
const A = '00000000-0000-4000-8000-00000000000a'
const B = '00000000-0000-4000-8000-00000000000b'

function mount() {
  const stub = stubClu()
  window.clu = { ...stub.clu, windowId: WIN }
  render(
    <div className="clu-sidebar">
      <SpaceTabs spaceId={SPACE} />
    </div>
  )
  const tab = (id: string, section: 'today' | 'pinned') => defaultTabState({ id, profileId: PROFILE, spaceId: SPACE, section, url: 'https://example.com/', title: id })
  act(() =>
    stub.push({
      kind: 'snapshot',
      rev: 1,
      state: {
        ...defaultState(),
        rev: 1,
        windows: { [WIN]: { ...defaultWindowState({ id: WIN, kind: 'main', private: false, activeSpaceId: SPACE }), activeTabId: A } },
        spaces: { [SPACE]: { ...defaultSpaceState({ id: SPACE, name: 'Home', icon: { kind: 'letter' }, color: 'blue', profileId: PROFILE, ephemeral: false }), pinned: [{ kind: 'tab', id: B }], today: [A] } },
        tabs: { [A]: tab(A, 'today'), [B]: tab(B, 'pinned') }
      }
    })
  )
  let rev = 1
  const patch = (...ops: PatchOp[]) => act(() => stub.push({ kind: 'patch', rev: ++rev, ops }))
  return { patch }
}

const rendersOf = (id: string) => vi.mocked(rowModel.rowView).mock.calls.filter(([t]) => t.id === id).length

afterEach(() => vi.restoreAllMocks())

describe('SpaceTabs render cost', () => {
  it('a title, favicon or loading patch re-renders only that row and reads no layout', () => {
    const { patch } = mount()
    const offsetTop = vi.spyOn(HTMLElement.prototype, 'offsetTop', 'get')
    const [a, b] = [rendersOf(A), rendersOf(B)]
    patch({ op: 'set', path: ['tabs', B, 'title'], value: 'New title' }, { op: 'set', path: ['tabs', B, 'loading'], value: true }, { op: 'set', path: ['tabs', B, 'favicon'], value: 'chrome://favicon2/?x' })
    expect(rendersOf(A)).toBe(a)
    expect(rendersOf(B)).toBeGreaterThan(b)
    expect(offsetTop).not.toHaveBeenCalled()
  })

  it('a subtitle that grows a row runs the row slide', () => {
    const { patch } = mount()
    const offsetTop = vi.spyOn(HTMLElement.prototype, 'offsetTop', 'get')
    patch({ op: 'set', path: ['tabs', B, 'subtitle'], value: 'Standup in 5 min' })
    expect(offsetTop).toHaveBeenCalled()
  })
})
