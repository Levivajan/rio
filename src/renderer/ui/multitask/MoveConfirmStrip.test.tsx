import { fireEvent, render, screen } from '@testing-library/react'
import { beforeEach, describe, expect, it } from 'vitest'
import type { MiniWindowState } from '../../../shared/state/multitask'
import { stubClu } from '../../../test-utils/stubClu'
import { MoveConfirmStrip } from './MoveConfirmStrip'

const mini: MiniWindowState = {
  tabId: 'page-1', targetSpaceId: 's2', targetSpaceName: 'Work', profileMismatch: true, confirming: true,
  url: 'https://example.com/', title: 'Example', favicon: null, loading: false, progress: 1, canGoBack: false
}

let stub: ReturnType<typeof stubClu>
beforeEach(() => {
  stub = stubClu({ 'mini.moveToSpace': () => null, 'mini.cancelMove': () => null })
  window.clu = stub.clu
})

describe('MoveConfirmStrip', () => {
  it('warns that logins will not carry over and focuses Move', () => {
    render(<MoveConfirmStrip mini={mini} />)
    expect(screen.getByRole('alertdialog', { name: 'Open in Work' })).toHaveTextContent("Work uses a different profile. Logins and form data won't carry over.")
    expect(screen.getByRole('button', { name: 'Move' })).toHaveFocus()
  })

  it('Move confirms the profile change for the space it names; Cancel sends mini.cancelMove', () => {
    render(<MoveConfirmStrip mini={mini} />)
    fireEvent.click(screen.getByRole('button', { name: 'Move' }))
    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    expect(stub.calls).toEqual([
      expect.objectContaining({ name: 'mini.moveToSpace', payload: { spaceId: 's2', confirmProfileChange: true } }),
      expect.objectContaining({ name: 'mini.cancelMove' })
    ])
  })
})
