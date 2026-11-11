import { describe, expect, it } from 'vitest'
import { APP_ICONS, DEFAULT_APP_ICON } from './appIcons'
import { readSetting, settingDef } from './settings'

describe('app icons', () => {
  it('lists the default first and six alternates, each id once', () => {
    expect(APP_ICONS.map((i) => i.label)).toEqual(['Flat warm', 'Sunset', 'Ocean', 'Flat blue', 'Rio sunset', 'Warm', 'Dusk'])
    expect(APP_ICONS[0].id).toBe(DEFAULT_APP_ICON)
    expect(new Set(APP_ICONS.map((i) => i.id)).size).toBe(7)
  })

  it('keeps appearance.appIcon global, reading a missing or junk value as Flat warm and keeping a stored Sunset', () => {
    expect(settingDef('appearance.appIcon')?.scope).toBe('global')
    expect(DEFAULT_APP_ICON).toBe('flatWarm')
    expect(readSetting('appearance.appIcon', undefined)).toBe('flatWarm')
    for (const junk of ['Ocean', 'neon', '', 7, null, { id: 'ocean' }]) expect(readSetting('appearance.appIcon', junk)).toBe('flatWarm')
    expect(readSetting('appearance.appIcon', 'airySun')).toBe('airySun')
    for (const { id } of APP_ICONS) expect(readSetting('appearance.appIcon', id)).toBe(id)
  })
})
