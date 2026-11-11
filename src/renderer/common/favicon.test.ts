import { describe, expect, it } from 'vitest'
import { safeIconSrc } from './favicon'

describe('safeIconSrc', () => {
  it("keeps Chromium's favicon2 URLs, Clu favicons and inline images", () => {
    const favicon2 = 'chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=https%3A%2F%2Fa.test%2F&allowGoogleServerFallback=0'
    expect(safeIconSrc(favicon2)).toBe(favicon2)
    expect(safeIconSrc('clu://favicon/abc')).toBe('clu://favicon/abc')
    expect(safeIconSrc('data:image/png;base64,xx')).toBe('data:image/png;base64,xx')
  })

  it('drops everything else', () => {
    for (const src of ['https://evil.test/x.png', 'javascript:alert(1)', 'chrome://favicon/https://a.test', 'chrome://favicon2', 'data:text/html;base64,xx', 42, null, undefined]) {
      expect(safeIconSrc(src)).toBeNull()
    }
    expect(safeIconSrc(`chrome://favicon2/?${'a'.repeat(40_000)}`)).toBeNull()
  })
})
