// Onboarding step order, the moves-step chord and the default-browser outcome (theme-t4).
import { describe, expect, it } from 'vitest'
import { STEPS, defaultBrowserOutcome, isSidebarChord, lookZoom, nextStep, prevStep, stepsFor } from './steps'

const key = (over: Partial<KeyboardEvent>) => ({ code: 'KeyS', key: 's', metaKey: true, ctrlKey: false, altKey: false, shiftKey: false, ...over })

describe('onboarding steps', () => {
  it('runs welcome, look, moves, default browser, import, done and stops at both ends', () => {
    expect(STEPS.indexOf('moves')).toBe(2)
    expect(nextStep('welcome')).toBe('look')
    expect(nextStep('default-browser')).toBe('import')
    expect(nextStep('import')).toBe('done')
    expect(nextStep('done')).toBe('done')
    expect(prevStep('look')).toBe('welcome')
    expect(prevStep('welcome')).toBe('welcome')
  })

  it('drops the Import step both ways once the switch screen showed', () => {
    expect(stepsFor(true)).toEqual(['welcome', 'look', 'moves', 'default-browser', 'done'])
    expect(stepsFor(false)).toEqual(STEPS)
    expect(nextStep('default-browser', true)).toBe('done')
    expect(prevStep('done', true)).toBe('default-browser')
    expect(nextStep('import', true)).toBe('done')
    expect(prevStep('import', true)).toBe('default-browser')
    expect(nextStep('done', true)).toBe('done')
    expect(prevStep('done')).toBe('import')
  })

  it('treats only Cmd+S as the sidebar chord', () => {
    expect(isSidebarChord(key({}))).toBe(true)
    expect(isSidebarChord(key({ code: '', key: 'S' }))).toBe(true)
    expect(isSidebarChord(key({ metaKey: false }))).toBe(false)
    expect(isSidebarChord(key({ shiftKey: true }))).toBe(false)
    expect(isSidebarChord(key({ ctrlKey: true }))).toBe(false)
    expect(isSidebarChord(key({ code: 'KeyT', key: 't' }))).toBe(false)
  })

  it('moves on after a request and leaves only Skip for not-packaged, not-implemented and errors', () => {
    expect(defaultBrowserOutcome({ ok: true, value: { status: 'requested' } })).toBe('advance')
    expect(defaultBrowserOutcome({ ok: true, value: { status: 'already-default' } })).toBe('advance')
    expect(defaultBrowserOutcome({ ok: true, value: { status: 'not-packaged' } })).toBe('unavailable')
    expect(defaultBrowserOutcome({ ok: false, disabled: true, code: 'not-implemented' })).toBe('unavailable')
    expect(defaultBrowserOutcome({ ok: false, disabled: false, code: 'rejected' })).toBe('unavailable')
  })
})

describe('the look step fits the whole theme editor', () => {
  it('zooms the 356 x 508 editor to the slot, never above full size, and keeps junk at 1', () => {
    // The smallest window (560 x 400): the slot is 292 tall beside a 300 wide column.
    expect(lookZoom(300, 292)).toBe(0.574)
    expect(lookZoom(1000, 1000)).toBe(1)
    expect(lookZoom(178, 1000)).toBe(0.5)
    expect(lookZoom(0, 300)).toBe(1)
    expect(lookZoom(Number.NaN, 300)).toBe(1)
    expect(lookZoom(300, -4)).toBe(1)
  })
})
