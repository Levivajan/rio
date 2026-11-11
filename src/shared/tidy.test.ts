import { describe, expect, it } from 'vitest'
import { siteKey, tidyOrder } from './tidy'

const t = (id: string, url: string, splitId: string | null = null) => ({ id, url, splitId })

describe('tidy today', () => {
  it('keys a URL by its site', () => {
    expect(siteKey('https://docs.google.com/a')).toBe('google.com')
    expect(siteKey('https://www.github.com/x')).toBe('github.com')
    expect(siteKey('https://news.bbc.co.uk/')).toBe('bbc.co.uk')
    expect(siteKey('http://127.0.0.1:8080/')).toBe('127.0.0.1')
    expect(siteKey('chrome://settings/')).toBe('settings')
    expect(siteKey('about:blank')).toBe('about:')
    expect(siteKey('not a url')).toBe('other:')
  })

  it('puts tabs of one site together, the biggest group first, singles in their own order', () => {
    const tabs = [
      t('a', 'https://github.com/1'),
      t('b', 'https://news.example.com/'),
      t('c', 'https://docs.google.com/1'),
      t('d', 'https://github.com/2'),
      t('e', 'https://mail.google.com/'),
      t('f', 'https://github.com/3'),
      t('g', 'https://weather.example.org/')
    ]
    expect(tidyOrder(tabs)).toEqual(['a', 'd', 'f', 'c', 'e', 'b', 'g'])
  })

  it('answers null when Today is already tidy, and keeps a split together', () => {
    expect(tidyOrder([t('a', 'https://a.test/1'), t('b', 'https://a.test/2'), t('c', 'https://b.test/')])).toBeNull()
    expect(tidyOrder([])).toBeNull()
    const split = [t('a', 'https://b.test/'), t('x', 'https://a.test/1', 's1'), t('y', 'https://c.test/', 's1'), t('z', 'https://a.test/2')]
    expect(tidyOrder(split)).toEqual(['x', 'y', 'z', 'a'])
  })
})
