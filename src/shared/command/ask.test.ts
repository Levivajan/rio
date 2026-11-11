import { describe, expect, it } from 'vitest'
import { aiOn } from '../ai'
import { ASSISTANTS, askUrl, assistantById, assistantByPrefix, firstResultUrl, middleEllipsis, offersAsk } from './ask'

describe('ask', () => {
  it('builds each assistant address with the text URL-encoded', () => {
    const [grok, chatgpt, claude] = [assistantById('grok'), assistantById('chatgpt'), assistantById('claude')]
    expect(ASSISTANTS.map((a) => a.name)).toEqual(['Grok', 'ChatGPT', 'Claude'])
    expect(askUrl(chatgpt, ' google it if you kn ')).toBe('https://chatgpt.com/?q=google%20it%20if%20you%20kn')
    expect(askUrl(claude, 'a & b = c? #1')).toBe('https://claude.ai/new?q=a%20%26%20b%20%3D%20c%3F%20%231')
    expect(askUrl(grok, 'héllo 🌍')).toBe('https://grok.com/?q=h%C3%A9llo%20%F0%9F%8C%8D')
    expect(askUrl(grok, '   ')).toBeNull()
  })

  it('never throws on junk text and caps long text without splitting a surrogate pair', () => {
    expect(askUrl(assistantById('chatgpt'), 'a\ud800b')).toBe('https://chatgpt.com/?q=a%EF%BF%BDb')
    const long = askUrl(assistantById('claude'), `${'x'.repeat(2047)}😀tail`) ?? ''
    expect(decodeURIComponent(long.split('?q=')[1] ?? '')).toBe('x'.repeat(2047))
  })

  it('reads junk stored assistants as ChatGPT, and a switch as on unless stored false', () => {
    expect(assistantById('claude').id).toBe('claude')
    expect(assistantById('gemini').id).toBe('chatgpt')
    expect(assistantById(7).id).toBe('chatgpt')
    expect(aiOn({}, 'ai.askRow')).toBe(true)
    expect(aiOn(undefined, 'ai.askRow')).toBe(true)
    expect(aiOn({ 'ai.tidyTitles': 'yes' }, 'ai.tidyTitles')).toBe(true)
    expect(aiOn({ 'ai.tidyTitles': false }, 'ai.tidyTitles')).toBe(false)
  })

  it('offers the row for any typed text, one word included, and never for an address', () => {
    expect(offersAsk('google it if you kn')).toBe(true)
    expect(offersAsk('hello')).toBe(true)
    expect(offersAsk('whyistheskyblue')).toBe(true)
    expect(offersAsk('github.com')).toBe(false)
    expect(offersAsk('https://a.test/x y')).toBe(false)
    expect(offersAsk('')).toBe(false)
    expect(offersAsk('   ')).toBe(false)
  })

  it('finds an assistant from four typed letters or its whole name', () => {
    expect(assistantByPrefix('grok')?.id).toBe('grok')
    expect(assistantByPrefix('GROK ')?.id).toBe('grok')
    expect(assistantByPrefix('chat')?.id).toBe('chatgpt')
    expect(assistantByPrefix('ChatGPT')?.id).toBe('chatgpt')
    expect(assistantByPrefix('clau')?.id).toBe('claude')
    expect(assistantByPrefix('cla')).toBeNull()
    expect(assistantByPrefix('claudes')).toBeNull()
    expect(assistantByPrefix('')).toBeNull()
  })

  it('opens the first result where the engine has an address for it', () => {
    expect(firstResultUrl('google', 'weeknight soup')).toBe('https://www.google.com/search?q=weeknight+soup&btnI=1')
    expect(firstResultUrl('duckduckgo', 'weeknight soup')).toBe('https://duckduckgo.com/?q=%5Cweeknight+soup')
    expect(firstResultUrl('bing', 'weeknight soup')).toBeNull()
    expect(firstResultUrl('junk', 'a')).toBe('https://www.google.com/search?q=a&btnI=1')
    expect(firstResultUrl('google', '  ')).toBeNull()
  })

  it('cuts long addresses in the middle', () => {
    expect(middleEllipsis('github.com')).toBe('github.com')
    const cut = middleEllipsis('accounts.google.com/signin/v2/challenge?mail&service=lso')
    expect(cut).toHaveLength(28)
    expect(cut.startsWith('accounts.go')).toBe(true)
    expect(cut.endsWith('ail&service=lso')).toBe(true)
  })
})
