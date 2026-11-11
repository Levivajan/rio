import { describe, expect, it } from 'vitest'
import { decodePunycode, displayHost, fullUrlText, isSafeLabel, MAX_DISPLAY, pillText, security } from './urlDisplay'

describe('displayHost', () => {
  it('shows the host without www.', () => {
    expect(displayHost('https://www.example.com/a')).toBe('example.com')
    expect(displayHost('https://docs.example.com/a?b=c#d')).toBe('docs.example.com')
    expect(displayHost('http://127.0.0.1:5173/shell/a.html')).toBe('127.0.0.1:5173')
    expect(displayHost('https://WWW.Example.COM./')).toBe('example.com')
  })

  it('keeps an IDN spoof label in ASCII', () => {
    // Cyrillic а followed by Latin "pple": mixed scripts.
    expect(displayHost('https://xn--pple-43d.com/')).toBe('xn--pple-43d.com')
    // All-Cyrillic аррӏе: one script, but every letter imitates a Latin one.
    expect(displayHost('https://xn--80ak6aa92e.com/')).toBe('xn--80ak6aa92e.com')
  })

  it('shows a safe IDN label in Unicode', () => {
    expect(displayHost('https://xn--mnchen-3ya.de/')).toBe('münchen.de')
    expect(displayHost('https://www.xn--e1afmkfd.xn--p1ai/')).toBe('пример.рф')
    expect(displayHost('https://xn--eckwd4c7c.xn--zckzah/')).toBe('ドメイン.テスト')
  })

  it('rejects malformed punycode', () => {
    expect(decodePunycode('99999999999')).toBeNull()
    expect(decodePunycode('ab-!')).toBeNull()
  })

  it('truncates an unparseable string to 200 characters', () => {
    const junk = `not a url ${'x'.repeat(5000)}`
    const shown = displayHost(junk)
    expect(shown).toHaveLength(MAX_DISPLAY)
    expect(shown.startsWith('not a url xx')).toBe(true)
    expect(displayHost('bad\u0000\nhost')).toBe('bad host')
    expect(displayHost(42 as unknown as string)).toBe('')
  })

  it('shows internal page titles and nothing for a blank tab', () => {
    expect(displayHost('rio://settings/')).toBe('Settings')
    expect(displayHost('rio://history/', 'History')).toBe('History')
    expect(displayHost('rio://about/', '')).toBe('About Rio')
    expect(displayHost('about:blank')).toBe('')
  })
})

describe('pillText', () => {
  it('shows Rio\'s own pages by their rio: URL and web pages by their host', () => {
    expect(pillText('rio://settings/', 'Settings')).toBe('rio://settings/')
    expect(pillText('rio://settings/appearance')).toBe('rio://settings/appearance')
    expect(pillText('https://www.example.com/a?b')).toBe('example.com')
    expect(pillText('about:blank')).toBe('')
    expect(pillText(`rio://history/${'x'.repeat(5000)}`)).toHaveLength(MAX_DISPLAY)
    expect(pillText('rio://set\u0000tings/')).not.toContain('\u0000')
    expect(pillText(7 as unknown as string)).toBe('')
  })
})

describe('fullUrlText', () => {
  it('shows a web page\'s whole URL with www., port, path, query and hash', () => {
    expect(fullUrlText('https://www.a.com:8443/x/y?q=1#top')).toBe('https://www.a.com:8443/x/y?q=1#top')
    expect(fullUrlText('http://127.0.0.1:4000/title1.html')).toBe('http://127.0.0.1:4000/title1.html')
  })
  it('decodes a safe IDN host and keeps a lookalike in punycode', () => {
    expect(fullUrlText('https://xn--mnchen-3ya.de/a')).toBe('https://münchen.de/a')
    expect(fullUrlText('https://xn--pple-43d.com/login')).toBe('https://xn--pple-43d.com/login')
  })
  it('falls back to the pill\'s text for Rio\'s pages, blank tabs and junk, and clips long URLs', () => {
    expect(fullUrlText('rio://settings/appearance')).toBe('rio://settings/appearance')
    expect(fullUrlText('about:blank')).toBe('')
    expect(fullUrlText('not a url\u0000')).toBe(pillText('not a url\u0000'))
    expect(fullUrlText(42 as unknown as string)).toBe('')
    expect(fullUrlText(`https://a.com/${'x'.repeat(1000)}`)).toHaveLength(MAX_DISPLAY)
  })
})

describe('isSafeLabel', () => {
  it('allows one script, CJK mixes and digits; rejects mixes and odd characters', () => {
    expect(isSafeLabel('münchen')).toBe(true)
    expect(isSafeLabel('пример2')).toBe(true)
    expect(isSafeLabel('日本語abc')).toBe(true)
    expect(isSafeLabel('аpple')).toBe(false)
    expect(isSafeLabel('paypal⁄com')).toBe(false)
    expect(isSafeLabel('αβγ')).toBe(true)
    expect(isSafeLabel('ορα')).toBe(false)
  })
})

describe('security', () => {
  it('returns secure, insecure and internal', () => {
    expect(security('https://example.com/')).toBe('secure')
    expect(security('http://example.com/')).toBe('insecure')
    expect(security('rio://settings/')).toBe('internal')
    expect(security('chrome://password-manager/passwords')).toBe('internal')
    expect(security('chrome-extension://abc/page.html')).toBe('insecure')
    expect(security('about:blank')).toBe('internal')
    expect(security('not a url')).toBe('insecure')
  })
})
