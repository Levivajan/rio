// @vitest-environment jsdom
// Zap selector generation (tweaks-t2): stable ids, hash-like ids and classes, nested identical lists, related.
import { afterEach, describe, expect, it } from 'vitest'
import { zapTarget } from './selector'

const $ = (sel: string) => document.querySelector(sel) as Element
const matches = (sel: string) => Array.from(document.querySelectorAll(sel))

afterEach(() => {
  document.body.innerHTML = ''
})

describe('zapTarget', () => {
  it('returns #main for a stable unique id', () => {
    document.body.innerHTML = '<div id="main"><p>Hi</p></div>'
    expect(zapTarget($('#main'))).toEqual({ selector: '#main', related: '#main', label: 'div · Hi' })
  })

  it('skips id x83920 and class css-1a2b3c', () => {
    document.body.innerHTML = '<section><div id="x83920" class="css-1a2b3c">A</div><div>B</div></section>'
    const t = zapTarget($('#x83920'))
    expect(t?.selector).not.toMatch(/x83920|css-1a2b3c/)
    expect(matches(t?.selector ?? '')).toEqual([$('#x83920')])
    // No stable class, so the related selector is the exact one.
    expect(t?.related).toBe(t?.selector)
  })

  it('treats other generated names as unstable and keeps readable ones', () => {
    document.body.innerHTML = '<div class="sc-bdVaJa _a1b2c3 Button__x9y8z7 card primary">A</div><div class="card">B</div>'
    const el = $('.primary')
    const t = zapTarget(el)
    expect(t?.selector).not.toMatch(/sc-|_a1b2c3|Button__/)
    expect(t?.related).toBe('div.card.primary')
    expect(matches(t?.selector ?? '')).toEqual([el])
  })

  it('matches exactly one element in a 3-level list of identical items', () => {
    const item = '<li class="item"><span class="t">Same</span></li>'
    const list = `<ul class="list">${item}${item}${item}</ul>`
    document.body.innerHTML = `<ul class="list"><li class="item">${list}</li><li class="item">${list}</li><li class="item">${list}</li></ul>`
    const all = matches('ul.list > li.item > ul.list > li.item > span.t')
    expect(all).toHaveLength(9)
    for (const el of all) {
      const t = zapTarget(el)
      expect(matches(t?.selector ?? '')).toEqual([el])
    }
  })

  it('anchors at a unique stable id on the way up', () => {
    document.body.innerHTML = '<div id="feed"><div><a>x</a></div><div><a>y</a></div></div><div><div><a>x</a></div></div>'
    const el = matches('#feed a')[1] as Element
    const t = zapTarget(el)
    expect(matches(t?.selector ?? '')).toEqual([el])
  })

  it('builds a related selector that matches all siblings with the same stable class', () => {
    document.body.innerHTML = '<div class="cards"><div class="card x12345">One</div><div class="card">Two</div><div class="card">Three</div></div><p class="card">Not a div</p>'
    const t = zapTarget($('.card'))
    expect(t?.related).toBe('div.card')
    expect(matches(t?.related ?? '')).toEqual(matches('.cards > .card'))
    expect(matches(t?.selector ?? '')).toEqual([$('.card')])
    expect(t?.label).toBe('div · One')
  })

  it('labels with the tag and at most 40 characters of trimmed text, and refuses body and detached nodes', () => {
    document.body.innerHTML = `<p>  ${'word '.repeat(20)}  </p><img>`
    expect(zapTarget($('p'))?.label).toBe(`p · ${'word '.repeat(8).trim()}`)
    expect(zapTarget($('img'))?.label).toBe('img')
    expect(zapTarget(document.body)).toBeNull()
    expect(zapTarget(document.documentElement)).toBeNull()
    expect(zapTarget(document.createElement('div'))).toBeNull()
  })

  it('keeps selectors and labels inside main\'s caps on pages with long generated names', () => {
    const long = 'x'.repeat(300)
    const tag = `my-${'el'.repeat(200)}`
    document.body.innerHTML = `<div class="${long} ${long}b"><div class="${long}"><${tag}>${'t'.repeat(100)}</${tag}><${tag}></${tag}></div></div>`
    const t = zapTarget($(tag))
    expect(t).not.toBeNull()
    expect(t?.selector.length).toBeLessThanOrEqual(1024)
    expect(t?.related.length).toBeLessThanOrEqual(1024)
    expect(t?.label.length).toBeLessThanOrEqual(80)
    expect(matches(t?.selector ?? '')).toEqual([$(tag)])
  })

  it('never emits characters that could leave a CSS rule', () => {
    document.body.innerHTML = '<div id="a{b}" class="c;d @e </f>"><span>x</span></div>'
    const t = zapTarget($('span'))
    expect(t?.selector).not.toMatch(/[{};@<]/)
    expect(t?.related).not.toMatch(/[{};@<]/)
  })
})
