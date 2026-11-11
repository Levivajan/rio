import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { Dialog } from './Dialog'
import { IconButton } from './IconButton'
import { MenuList, placeSubmenu } from './MenuList'
import { TreeRow } from './TreeRow'

describe('primitives carry design.md roles', () => {
  it('TreeRow is a treeitem with aria-selected and aria-level', () => {
    render(<div role="tree"><TreeRow selected level={2}>Row</TreeRow></div>)
    const row = screen.getByRole('treeitem')
    expect(row).toHaveAttribute('aria-selected', 'true')
    expect(row).toHaveAttribute('aria-level', '2')
  })
  it('IconButton requires and exposes its label', () => {
    render(<IconButton aria-label="Close tab">x</IconButton>)
    expect(screen.getByRole('button', { name: 'Close tab' })).toBeInTheDocument()
  })
  it('Dialog is modal, labelled and cancels on Esc', () => {
    const onCancel = vi.fn()
    render(<Dialog title="Delete space?" onCancel={onCancel}><button type="button">OK</button></Dialog>)
    const dialog = screen.getByRole('dialog', { name: 'Delete space?' })
    expect(dialog).toHaveAttribute('aria-modal', 'true')
    fireEvent.keyDown(dialog, { key: 'Escape' })
    expect(onCancel).toHaveBeenCalled()
  })
  it('MenuList opens a second-level submenu and marks its parent', () => {
    const onPick = vi.fn()
    render(
      <MenuList
        items={[{ kind: 'item', id: 'move', label: 'Move To', submenu: [{ kind: 'item', id: 'space', label: 'Work', submenu: [{ kind: 'item', id: 'today', label: 'Today' }] }] }]}
        onPick={onPick}
        onClose={() => undefined}
      />
    )
    const parent = screen.getByRole('menuitem', { name: /Move To/ })
    expect(parent).toHaveAttribute('aria-haspopup', 'menu')
    fireEvent.click(parent)
    fireEvent.click(screen.getByRole('menuitem', { name: /Work/ }))
    fireEvent.click(screen.getByRole('menuitem', { name: 'Today' }))
    expect(onPick).toHaveBeenCalledWith('today')
    expect(screen.getAllByRole('menu')).toHaveLength(3)
  })
  it('MenuList: a submenu the pointer opens highlights nothing; one opened by a key focuses its first item', () => {
    const items = [{ kind: 'item' as const, id: 'move', label: 'Move To', submenu: [{ kind: 'item' as const, id: 'work', label: 'Work' }] }]
    const { unmount } = render(<MenuList items={items} onPick={() => undefined} onClose={() => undefined} />)
    fireEvent.pointerEnter(screen.getByRole('menuitem', { name: /Move To/ }))
    expect(screen.getByRole('menuitem', { name: 'Work' })).not.toHaveFocus()
    expect(screen.getByRole('menuitem', { name: /Move To/ })).toHaveAttribute('aria-expanded', 'true')
    unmount()
    render(<MenuList items={items} onPick={() => undefined} onClose={() => undefined} />)
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'ArrowRight' })
    expect(screen.getByRole('menuitem', { name: 'Work' })).toHaveFocus()
  })
  it('MenuList without focusFirst opens with nothing highlighted; ArrowDown reaches the first item', () => {
    render(<MenuList items={[{ kind: 'item', id: 'a', label: 'Copy Link' }, { kind: 'item', id: 'b', label: 'Rename' }]} focusFirst={false} onPick={() => undefined} onClose={() => undefined} />)
    expect(screen.getByRole('menu')).toHaveFocus()
    fireEvent.keyDown(screen.getByRole('menu'), { key: 'ArrowDown' })
    expect(screen.getByRole('menuitem', { name: 'Copy Link' })).toHaveFocus()
  })
  it('placeSubmenu flips left near the right edge and up near the bottom; its first item is level with the parent', () => {
    expect(placeSubmenu({ left: 900, right: 1100, top: 10 }, { width: 220, height: 100 }, { width: 1280, height: 800 })).toEqual({ left: 680, top: 5, flipLeft: true })
    expect(placeSubmenu({ left: 100, right: 300, top: 3 }, { width: 220, height: 100 }, { width: 1280, height: 800 })).toMatchObject({ top: 0 })
    expect(placeSubmenu({ left: 100, right: 300, top: 780 }, { width: 220, height: 100 }, { width: 1280, height: 800 })).toEqual({ left: 300, top: 700, flipLeft: false })
  })
})
