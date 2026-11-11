import { describe, expect, it } from 'vitest'
import { readSwitcherProps } from './TabSwitcher'

describe('readSwitcherProps', () => {
  it('drops junk cards and images that are not data or favicon URLs', () => {
    const r = readSwitcherProps({
      cards: [
        { tabId: 'a', title: 'A', host: 'a.test', favicon: 'clu://favicon/abc', thumb: 'data:image/jpeg;base64,xx' },
        { tabId: '', title: 'no id' },
        null,
        'junk',
        { tabId: 'b', title: 42, favicon: 'javascript:alert(1)', thumb: 'https://evil.test/x.png' }
      ],
      selected: 1
    })
    expect(r.cards).toEqual([
      { tabId: 'a', title: 'A', host: 'a.test', favicon: 'clu://favicon/abc', thumb: 'data:image/jpeg;base64,xx' },
      { tabId: 'b', title: '', host: '', favicon: null, thumb: null }
    ])
    expect(r.selected).toBe(1)
  })

  it("shows Chromium's favicon2 icons the browser sends", () => {
    const favicon = 'chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=https%3A%2F%2Fa.test%2F&allowGoogleServerFallback=0'
    const r = readSwitcherProps({ cards: [{ tabId: 'a', title: 'A', host: 'a.test', favicon, thumb: null }], selected: 0 })
    expect(r.cards[0]?.favicon).toBe(favicon)
  })

  it('reads missing or bad props as no cards and no selection', () => {
    expect(readSwitcherProps(null)).toEqual({ cards: [], selected: -1 })
    expect(readSwitcherProps({ cards: 'x', selected: 1.5 })).toEqual({ cards: [], selected: -1 })
  })
})
