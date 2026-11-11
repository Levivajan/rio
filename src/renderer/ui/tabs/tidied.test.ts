import { beforeEach, describe, expect, it } from 'vitest'
import { TIDY_MORPH_MS, TIDY_SUBTITLE_MS, TIDY_WINDOW_MS, observeTidy, resetTidyForTesting, tidiedJustNow, tidyExpiresIn } from './tidied'

describe('observeTidy', () => {
  beforeEach(resetTidyForTesting)

  it('marks a row tidied when its title is renamed soon after it is pinned, until the title changes', () => {
    expect(observeTidy({ id: 't', section: 'today', titleOverride: null }, false, 0)).toBe(false)
    expect(observeTidy({ id: 't', section: 'pinned', titleOverride: null }, false, 10)).toBe(false)
    expect(observeTidy({ id: 't', section: 'pinned', titleOverride: 'Rio de Janeiro' }, false, 1200)).toBe(true)
    expect(observeTidy({ id: 't', section: 'pinned', titleOverride: 'Rio de Janeiro' }, false, 20_000)).toBe(true)
    expect(observeTidy({ id: 't', section: 'pinned', titleOverride: 'Mine' }, false, 21_000)).toBe(false)
  })

  it('also catches a tidy title that arrives with the pin', () => {
    observeTidy({ id: 'd', section: 'today', titleOverride: null }, true, 0)
    expect(observeTidy({ id: 'd', section: 'pinned', titleOverride: 'Tidy' }, true, 5)).toBe(true)
  })

  it('drops the hint once the user leaves the tidied row, and after TIDY_SUBTITLE_MS', () => {
    observeTidy({ id: 'e', section: 'today', titleOverride: null }, true, 0)
    expect(observeTidy({ id: 'e', section: 'pinned', titleOverride: 'Apple Products' }, true, 900)).toBe(true)
    expect(observeTidy({ id: 'e', section: 'pinned', titleOverride: 'Apple Products' }, false, 2000)).toBe(false)
    expect(observeTidy({ id: 'e', section: 'pinned', titleOverride: 'Apple Products' }, true, 3000)).toBe(false)
    observeTidy({ id: 'f', section: 'today', titleOverride: null }, false, 0)
    expect(observeTidy({ id: 'f', section: 'pinned', titleOverride: 'Weather Alerts' }, false, 1000)).toBe(true)
    expect(tidyExpiresIn('f', 1000)).toBe(TIDY_SUBTITLE_MS)
    expect(observeTidy({ id: 'f', section: 'pinned', titleOverride: 'Weather Alerts' }, false, 1000 + TIDY_SUBTITLE_MS)).toBe(false)
    expect(tidyExpiresIn('f')).toBeNull()
  })

  it('morphs the tidy title only right after it arrives', () => {
    observeTidy({ id: 'g', section: 'today', titleOverride: null }, true, 0)
    observeTidy({ id: 'g', section: 'pinned', titleOverride: 'Testing Domain' }, true, 1000)
    expect(tidiedJustNow('g', 1000)).toBe(true)
    expect(tidiedJustNow('g', 1000 + TIDY_MORPH_MS)).toBe(false)
    expect(tidiedJustNow('nobody', 0)).toBe(false)
  })

  it('ignores renames long after pinning, rows pinned with a title already, and Today rows', () => {
    observeTidy({ id: 'a', section: 'today', titleOverride: null }, false, 0)
    observeTidy({ id: 'a', section: 'pinned', titleOverride: null }, false, 0)
    expect(observeTidy({ id: 'a', section: 'pinned', titleOverride: 'Late' }, false, TIDY_WINDOW_MS + 1)).toBe(false)
    observeTidy({ id: 'b', section: 'today', titleOverride: 'Named' }, false, 0)
    expect(observeTidy({ id: 'b', section: 'pinned', titleOverride: 'Named' }, false, 5)).toBe(false)
    expect(observeTidy({ id: 'c', section: 'today', titleOverride: 'x' }, false, 0)).toBe(false)
  })
})
