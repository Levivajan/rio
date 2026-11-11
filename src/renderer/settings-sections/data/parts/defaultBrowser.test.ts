// The Default browser row's text and button for each app.defaultBrowser value and request answer.
import { describe, expect, it } from 'vitest'
import { defaultBrowserRow } from './defaultBrowser'

describe('defaultBrowserRow', () => {
  it('says Rio is the default and hides the button once macOS says so, whatever was asked', () => {
    expect(defaultBrowserRow('default', null)).toEqual({ text: 'Rio is your default browser.', canAsk: false })
    expect(defaultBrowserRow('default', 'requested').canAsk).toBe(false)
    expect(defaultBrowserRow('default', 'not-packaged').canAsk).toBe(false)
  })

  it('offers the button while Rio is not the default, and after a request until macOS answers', () => {
    expect(defaultBrowserRow('not-default', null)).toEqual({ text: 'Rio isn’t your default browser.', canAsk: true })
    expect(defaultBrowserRow('not-default', 'requested')).toEqual({ text: 'Confirm the change in the dialog macOS shows.', canAsk: true })
    expect(defaultBrowserRow('unavailable', null).canAsk).toBe(true)
  })

  it('drops the button for a build macOS cannot name, and keeps it after an error', () => {
    expect(defaultBrowserRow('unavailable', 'not-packaged')).toEqual({
      text: 'Install Rio in Applications to make it your default browser.',
      canAsk: false
    })
    expect(defaultBrowserRow('not-default', 'failed').canAsk).toBe(true)
  })
})
