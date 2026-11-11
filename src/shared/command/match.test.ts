import { describe, expect, it } from 'vitest'
import { buildIndex, charMask, fold, matchIndex, MAX_TITLE, queryTokens, type IndexInput } from './match'

const row = (url: unknown, title: unknown, frecency = 1, lastVisitAt = 0): IndexInput => ({ url, title, frecency, lastVisitAt })
const slice = (s: string, r: readonly [number, number]) => s.slice(r[0], r[1])

describe('fold', () => {
  it('lowercases, decomposes and strips combining marks', () => {
    expect(fold('Café').text).toBe('cafe')
    expect(fold('ÉCOLE Ünïcode').text).toBe('ecole unicode')
    expect(fold('ﬁle').text).toBe('file')
  })
  it('maps folded positions back to the original', () => {
    const f = fold('ﬁle Café')
    expect(f.map).not.toBeNull()
    expect(fold('plain ascii').map).toBeNull()
    expect(fold('Café').map).toBeNull()
  })
})

describe('charMask', () => {
  it('sets letter bits and digit buckets and ignores the rest', () => {
    expect(charMask('a')).toBe(1)
    expect(charMask('z')).toBe(1 << 25)
    expect(charMask('0') & charMask('6')).toBe(charMask('0'))
    expect(charMask('é-/')).toBe(0)
  })
})

describe('buildIndex', () => {
  it('skips and counts rows with a null, empty, unparseable or non-http URL', () => {
    const idx = buildIndex([
      row('https://ok.example/', 'Ok'),
      row(null, 'null url'),
      row('', 'empty'),
      row('ftp://files.example/a', 'ftp'),
      row('javascript:alert(1)', 'js'),
      row('http://', 'no host'),
      row(42, 'number'),
      row('https://' + 'a'.repeat(40_000) + '.com/', 'huge'),
      null as unknown as IndexInput,
      row('http://two.example/x', 'Two')
    ])
    expect(idx.size).toBe(2)
    expect(idx.skipped).toBe(8)
    expect(Array.from(idx.source)).toEqual([0, 9])
  })
  it('cleans titles: control characters stripped, clamped, non-strings empty', () => {
    const idx = buildIndex([row('https://a.example/', 'a\u0000b‮c\n'), row('https://b.example/', 'x'.repeat(5000)), row('https://c.example/', { evil: 1 })])
    expect(idx.title[0]).toBe('abc')
    expect(idx.title[1]).toHaveLength(MAX_TITLE)
    expect(idx.title[2]).toBe('')
  })
  it('keeps the URL text without scheme and www, and the host length', () => {
    const idx = buildIndex([row('https://www.github.com/foo/bar?x=1', 'GitHub'), row('http://localhost:5173/a', 'Local'), row('https://github.com/', 'Root')])
    // A bare host drops its slash: github.com.
    expect(idx.urlText).toEqual(['github.com/foo/bar?x=1', 'localhost:5173/a', 'github.com'])
    expect(Array.from(idx.hostLen)).toEqual(['github.com'.length, 'localhost:5173'.length, 'github.com'.length])
  })
})

describe('matchIndex', () => {
  const idx = buildIndex([
    row('https://github.com/', 'GitHub', 10, 5),
    row('https://news.example/cafe', 'Le Café de Flore', 1, 4),
    row('https://mail.google.com/mail/u/0/', 'Inbox (3) - Gmail', 5, 3),
    row('https://docs.example/programming', 'Programming guide', 1, 2),
    row('https://shop.example/', 'Shop', 1, 1)
  ])
  const titles = (q: string) => matchIndex(idx, q).hits.map((h) => idx.title[h.i])

  it("matches accented titles from plain text: 'cafe' matches 'Café'", () => {
    const { hits } = matchIndex(idx, 'cafe')
    expect(hits.map((h) => idx.title[h.i])).toContain('Le Café de Flore')
    const h = hits.find((x) => idx.title[x.i] === 'Le Café de Flore')
    expect(h && slice(idx.title[h.i] ?? '', h.titleRanges[0] ?? [0, 0])).toBe('Café')
  })

  it('scores prefix above word prefix above substring above subsequence', () => {
    const one = buildIndex([row('https://x.example/', 'github'), row('https://y.example/', 'my github'), row('https://z.example/', 'mygithub'), row('https://w.example/', 'g-i-t-h-u-b')])
    const m = matchIndex(one, 'github').hits.map((h) => [one.title[h.i], h.match])
    expect(m).toEqual([['github', 1], ['my github', 0.85], ['mygithub', 0.6], ['g-i-t-h-u-b', expect.any(Number)]])
    const sub = m[3]?.[1] as number
    expect(sub).toBeGreaterThanOrEqual(0.3)
    expect(sub).toBeLessThanOrEqual(0.5)
  })

  it('requires every token (AND) and accepts tokens in any field', () => {
    expect(titles('gmail inbox')).toEqual(['Inbox (3) - Gmail'])
    expect(titles('google inbox')).toEqual(['Inbox (3) - Gmail'])
    expect(titles('gmail shop')).toEqual([])
  })

  it('returns highlight ranges in the title and the URL text', () => {
    const { hits } = matchIndex(idx, 'git')
    const h = hits[0]
    expect(h && idx.title[h.i]).toBe('GitHub')
    expect(h?.titleRanges).toEqual([[0, 3]])
    const u = matchIndex(idx, 'mail.google').hits[0]
    expect(u && slice(idx.urlText[u.i] ?? '', u.urlRanges[0] ?? [0, 0])).toBe('mail.google')
    const s = matchIndex(idx, 'pgm').hits.find((x) => idx.title[x.i] === 'Programming guide')
    expect(s?.titleRanges.map((r) => slice('Programming guide', r))).toEqual(['P', 'g', 'm'])
  })

  it('maps ranges through characters that fold to a different length', () => {
    const lig = buildIndex([row('https://a.example/', 'Big ﬁle Café')])
    const h = matchIndex(lig, 'file cafe').hits[0]
    expect(h?.titleRanges.map((r) => slice('Big ﬁle Café', r))).toEqual(['ﬁle', 'Café'])
  })

  it('flags strong hits: word prefixes in the title or host, not loose or path-only hits', () => {
    const strong = (q: string) => matchIndex(idx, q).hits.map((h) => [idx.title[h.i], h.strong])
    expect(strong('gmail')).toEqual([['Inbox (3) - Gmail', true]])
    expect(strong('google')).toEqual([['Inbox (3) - Gmail', true]])
    expect(strong('gm')).toContainEqual(['Programming guide', false])
    const path = buildIndex([row('https://x.example/gmail', 'Other')])
    expect(matchIndex(path, 'gmail').hits[0]?.strong).toBe(false)
  })

  it('adds frecency and boost to the score and breaks ties on lastVisitAt', () => {
    const tie = buildIndex([row('https://a.example/', 'Alpha', 1, 1), row('https://b.example/', 'Alpha', 1, 9), { ...row('https://c.example/', 'Alpha', 1, 0), boost: 10 }])
    expect(matchIndex(tie, 'alpha').hits.map((h) => h.i)).toEqual([2, 1, 0])
    const freq = buildIndex([row('https://a.example/', 'Beta', 1), row('https://b.example/', 'Beta', 10)])
    const hits = matchIndex(freq, 'beta').hits
    expect(hits[0]?.i).toBe(1)
    expect(hits[0]?.score).toBeCloseTo(120)
  })

  it('keeps only the K best', () => {
    const many = buildIndex(Array.from({ length: 100 }, (_, i) => row(`https://s${i}.example/`, `Item ${i}`, i)))
    const { hits } = matchIndex(many, 'item', 5)
    expect(hits.map((h) => h.i)).toEqual([99, 98, 97, 96, 95])
  })

  it('narrows from the previous survivors when the query extends it', () => {
    const first = matchIndex(idx, 'g')
    const next = matchIndex(idx, 'gm', 16, first.state)
    expect(next.state.survivors.length).toBeLessThanOrEqual(first.state.survivors.length)
    expect(next.hits.map((h) => h.i)).toEqual(matchIndex(idx, 'gm').hits.map((h) => h.i))
    // A query that does not extend the last one, or another index, scans everything.
    const other = matchIndex(idx, 'shop', 16, next.state)
    expect(other.hits.map((h) => idx.title[h.i])).toEqual(['Shop'])
  })

  it('handles empty, huge and odd queries without throwing', () => {
    expect(matchIndex(idx, '').hits).toEqual([])
    expect(matchIndex(idx, '   ').hits).toEqual([])
    expect(matchIndex(idx, 'x'.repeat(10_000)).hits).toEqual([])
    expect(() => matchIndex(idx, '𐏿\ud800 a')).not.toThrow()
    expect(queryTokens('https://www.GitHub.com/x  y')).toEqual(['github.com/x', 'y'])
    expect(queryTokens(42 as unknown as string)).toEqual([])
  })
})
