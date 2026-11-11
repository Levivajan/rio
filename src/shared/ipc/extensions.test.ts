import { describe, expect, it } from 'vitest'
import { extensionsIpc } from './extensions'

describe('extensions IPC', () => {
  it('lets the Mini window toolbar open Site controls, and its popover in the overlay turn blocking off', () => {
    expect(extensionsIpc['siteControls.open'].allow).toEqual(['ui', 'mini-ui'])
    expect(extensionsIpc['blocking.setSiteEnabled'].allow).toContain('overlay')
  })
})
