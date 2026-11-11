// A split row inside a folder takes the folder's indent at every level, as a tab row does, and a collapsed folder
// that holds the active split still shows the whole split at its child indent.
import { act, cleanup, render } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { App } from '../App'
import { installBridge } from '../../common/clu'
import { setStateMirrorForTest } from '../../common/store'
import { createFakeHost } from '../../common/bridge/fake'
import { FAKE } from '../../common/bridge/fake-seed'

const NESTED = '00000000-0000-4000-8000-0004000000f9'

afterEach(() => {
  cleanup()
  installBridge(null)
  vi.unstubAllGlobals()
})

async function mount() {
  vi.stubGlobal('ResizeObserver', class { observe() {} unobserve() {} disconnect() {} })
  const host = createFakeHost()
  const ui = host.connect({ surface: 'ui' })
  installBridge(ui)
  setStateMirrorForTest(null)
  const view = render(<App />)
  // The seeded folder holds tabs 12 and 13; a folder inside it holds tabs 10 and 11.
  host.edit(['folders', NESTED], { id: NESTED, spaceId: FAKE.spaces.home, name: 'Nested', icon: null, collapsed: false, children: [] })
  host.edit(['folders', FAKE.folder, 'children'], [...(host.state().folders[FAKE.folder]?.children ?? []), { kind: 'folder', id: NESTED }])
  for (const [index, n] of [10, 11].entries()) await ui.tabs.move({ nodeId: FAKE.tab(n), target: { section: 'pinned', spaceId: FAKE.spaces.home, parentId: NESTED, index } })
  const split = async (a: number, b: number) => {
    const made = await ui.split.create({ tabIds: [FAKE.tab(a), FAKE.tab(b)], dir: 'row' })
    if (!made.ok) throw new Error('split.create failed')
    return made.value.splitId
  }
  const ids = { top: await split(20, 21), inFolder: await split(12, 13), inNested: await split(10, 11) }
  await act(async () => {
    host.flush()
    await Promise.resolve()
  })
  const row = (id: string) => view.container.querySelector<HTMLElement>(`[data-split-id="${id}"]`)
  return { host, ui, ids, row }
}

describe('SplitSidebarRow in folders', () => {
  it('takes the folder indent per level: none at the top, 16 in a folder, 32 in a nested folder', async () => {
    const { ids, row } = await mount()
    expect(row(ids.top)?.getAttribute('aria-level')).toBe('1')
    expect(row(ids.top)?.style.marginInlineStart).toBe('calc(0 * var(--folder-indent))')
    expect(row(ids.inFolder)?.getAttribute('aria-level')).toBe('2')
    expect(row(ids.inFolder)?.style.marginInlineStart).toBe('calc(1 * var(--folder-indent))')
    expect(row(ids.inNested)?.getAttribute('aria-level')).toBe('3')
    expect(row(ids.inNested)?.style.marginInlineStart).toBe('calc(2 * var(--folder-indent))')
    expect(row(ids.inNested)?.getAttribute('data-active')).toBe('true')
  })

  it('stays whole under a collapsed folder that holds it, at that folder\'s child indent', async () => {
    const { host, ui, ids, row } = await mount()
    await ui.folders.setCollapsed({ folderId: NESTED, collapsed: true })
    await act(async () => {
      host.flush()
      await Promise.resolve()
    })
    expect(row(ids.inNested)?.getAttribute('aria-level')).toBe('3')
    expect(row(ids.inNested)?.querySelectorAll('[data-split-member]')).toHaveLength(2)
    await ui.folders.setCollapsed({ folderId: FAKE.folder, collapsed: true })
    await act(async () => {
      host.flush()
      await Promise.resolve()
    })
    expect(row(ids.inNested)?.getAttribute('aria-level')).toBe('2')
    expect(row(ids.inNested)?.style.marginInlineStart).toBe('calc(1 * var(--folder-indent))')
    expect(row(ids.inNested)?.querySelectorAll('[data-split-member]')).toHaveLength(2)
    expect(row(ids.inFolder)).toBeNull()
  })
})
