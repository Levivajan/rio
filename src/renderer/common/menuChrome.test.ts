import { afterEach, describe, expect, it } from 'vitest'
import { menuLight } from './menuChrome'

describe('menuLight', () => {
  afterEach(() => void delete document.documentElement.dataset.theme)
  it('follows the chrome set the WebUI wears, not macOS', () => {
    document.documentElement.dataset.theme = 'light'
    expect(menuLight()).toBe(true)
    document.documentElement.dataset.theme = 'dark'
    expect(menuLight()).toBe(false)
  })
  it('is false before any chrome set is written', () => {
    expect(menuLight()).toBe(false)
  })
})
