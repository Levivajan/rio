import { fireEvent, render } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import type { OverlaySurfaceState } from '../../../shared/state'
import type { Json } from '../../../shared/types'
import { stubClu } from '../../../test-utils/stubClu'
import { mailLink, mailTime, TabHoverCard } from './TabHoverCard'

const card = (props: Record<string, unknown>) =>
  render(<TabHoverCard windowId="w" instance={{ instanceId: 'i', surface: 'tab-hover', layer: 'passive', modality: 'passive', focus: false, openedFrom: null, slot: 2, rect: { x: 0, y: 0, width: 255, height: 80 }, props: props as Json } as OverlaySurfaceState} />)

// A Gmail favorite's preview card says how much unread mail there is; junk counts read as none.
describe('TabHoverCard mail preview', () => {
  const item = (n: number) => ({ author: `Sender ${n}`, title: `Subject ${n}`, issued: `2026-11-10T1${n}:00:00Z`, url: `https://mail.google.com/mail?message_id=${n}` })
  const mail = (props: Record<string, unknown>) => card({ kind: 'mail', site: 'Gmail', title: 'Inbox', width: 255, height: 80, ...props })

  it('is the design card with no unread mail: two lines, the leaf on the second', () => {
    for (const mailSource of ['feed', 'title']) {
      const view = mail({ unread: 0, mailSource, mailItems: [] })
      const exhale = view.container.querySelector('.clu-mail-exhale')
      expect(exhale?.textContent).toBe('No unread emails, you can exhale!\u{1F343}')
      expect(exhale?.querySelector('br')).not.toBeNull()
      view.unmount()
    }
  })

  it('lists the newest unread messages from the feed, sender, subject and time, at most 5', () => {
    const view = mail({ unread: 40, mailSource: 'feed', mailItems: [1, 2, 3, 4, 5, 6].map(item) })
    expect(view.container.querySelector('.clu-mail-head')?.textContent).toBe('40 unread emails')
    const rows = [...view.container.querySelectorAll('.clu-mail-row')]
    expect(rows).toHaveLength(5)
    expect(rows[0]?.querySelector('.clu-mail-from')?.textContent).toBe('Sender 1')
    expect(rows[0]?.querySelector('.clu-mail-subject')?.textContent).toBe('Subject 1')
    expect(rows[0]?.querySelector('.clu-mail-time')?.textContent).not.toBe('')
    expect(view.container.textContent).not.toContain('No unread')
  })

  it('shows the count alone when the feed was unavailable (3 and 40 from the title)', () => {
    expect(mail({ unread: 3, mailSource: 'title' }).getByRole('tooltip').textContent).toBe('3 unread emails')
    expect(mail({ unread: 40, mailSource: 'title' }).getAllByRole('tooltip')[1]?.textContent).toBe('40 unread emails')
    expect(mail({ unread: 1, mailSource: 'title' }).getAllByRole('tooltip')[2]?.textContent).toBe('1 unread email')
  })

  it('never says "No unread emails" without a source that says so, and reads junk as no count', () => {
    for (const unread of [0, -4, Number.NaN, '7', null]) {
      const view = mail({ unread, mailSource: 'unknown' })
      expect(view.container.textContent).toBe('Open Gmail to check for unread email')
      view.unmount()
    }
    const junk = mail({ unread: 2, mailSource: 'feed', mailItems: ['x', null, { author: 7, title: ['t'] }] })
    expect(junk.container.querySelectorAll('.clu-mail-row')).toHaveLength(1)
    expect(junk.container.querySelector('.clu-mail-from')?.textContent).toBe('Unknown sender')
    expect(junk.container.querySelector('.clu-mail-subject')?.textContent).toBe('(no subject)')
  })

  it('shows the time of day for today and the date before it, nothing for junk', () => {
    const now = new Date('2026-11-10T22:00:00')
    expect(mailTime('2026-11-10T09:05:00', now)).toBe('9:05 AM')
    expect(mailTime('2026-11-08T09:05:00', now)).toBe('Nov 8')
    expect(mailTime('soon', now)).toBe('')
    expect(mailTime('', now)).toBe('')
  })

  it('keeps the title pill for other pages', () => {
    const view = card({ kind: 'title', title: 'Docs', width: 336, height: 44 })
    expect(view.container.querySelector('.clu-hover-card')?.textContent).toBe('Docs')
    expect(view.container.querySelector('.clu-mail-card')).toBeNull()
  })
})

// A preview site's card is the frame the browser lays the live compact page in; it never draws a snapshot.
describe('TabHoverCard live preview', () => {
  const props = { kind: 'preview', site: 'Google Calendar', title: 'Calendar', width: 255, height: 272 }
  it('draws the frame with the site name and no page image, even when props carry one', () => {
    for (const image of [undefined, 'data:image/jpeg;base64,AAAA', 'https://evil.test/x.png']) {
      const view = card({ ...props, image })
      expect(view.getByRole('tooltip').getAttribute('aria-label')).toBe('Google Calendar preview')
      expect(view.container.querySelector('img')).toBeNull()
      expect(view.container.textContent).toBe('Google Calendar')
      view.unmount()
    }
  })
})

// The title pill fades out over 90 ms while the browser keeps it marked closing; it runs once.
describe('TabHoverCard fade out', () => {
  it('marks only a closing title pill, and only for a real true', () => {
    const open = card({ kind: 'title', title: 'Docs', width: 336, height: 44 })
    expect(open.container.querySelector('.clu-hover-card')?.hasAttribute('data-closing')).toBe(false)
    open.unmount()
    for (const closing of ['true', 1, null]) {
      const view = card({ kind: 'title', title: 'Docs', width: 336, height: 44, closing })
      expect(view.container.querySelector('.clu-hover-card')?.hasAttribute('data-closing')).toBe(false)
      view.unmount()
    }
    const closing = card({ kind: 'title', title: 'Docs', width: 336, height: 44, closing: true })
    expect(closing.container.querySelector('.clu-hover-card')?.getAttribute('data-closing')).toBe('true')
  })

  it('keeps the closing pill the same element through a re-render, so its fade never restarts', () => {
    const props = { kind: 'title', title: 'Docs', width: 336, height: 44, closing: true }
    const instance = { instanceId: 'i', surface: 'tab-hover', layer: 'passive', modality: 'passive', focus: false, openedFrom: null, slot: 2, rect: { x: 0, y: 0, width: 255, height: 80 } }
    const view = render(<TabHoverCard windowId="w" instance={{ ...instance, props } as OverlaySurfaceState} />)
    const el = view.container.querySelector('.clu-hover-card')
    view.rerender(<TabHoverCard windowId="w" instance={{ ...instance, props: { ...props } } as OverlaySurfaceState} />)
    expect(view.container.querySelector('.clu-hover-card')).toBe(el)
  })
})

// Gmail's message list is usable: it holds itself open while the pointer is on it, and a click on a message
// opens it in the row's own tab (tabs.openMail); only Gmail links open.
describe('TabHoverCard mail list pointer', () => {
  const TAB = '00000000-0000-4000-8000-00000000000a'
  const items = [
    { author: 'A', title: 'One', issued: '', url: 'https://mail.google.com/mail?account_id=x&message_id=1&view=conv&extsrc=atom' },
    { author: 'B', title: 'Two', issued: '', url: 'javascript:alert(1)' },
    { author: 'C', title: 'Three', issued: '', url: 'https://mail.google.com.evil.test/x' }
  ]
  const mount = (props: Record<string, unknown> = {}) => {
    const stub = stubClu({ 'tabs.hover': () => null, 'tabs.openMail': () => null })
    window.clu = { ...stub.clu, windowId: 'w' }
    const view = card({ kind: 'mail', site: 'Gmail', title: 'Inbox', width: 255, height: 200, unread: 3, mailSource: 'feed', mailItems: items, tabId: TAB, ...props })
    const of = (name: string) => stub.calls.filter((c) => c.name === name).map((c) => c.payload)
    return { view, of }
  }

  it('holds the card while the pointer is on it and lets go as it leaves', () => {
    const { view, of } = mount()
    const list = view.container.querySelector('.clu-mail-list') as HTMLElement
    fireEvent.pointerEnter(list)
    fireEvent.pointerLeave(list)
    expect(of('tabs.hover')).toEqual([{ tabId: TAB, held: true }, { tabId: TAB, held: false }])
  })

  it('opens a clicked message in the row\'s own tab, and only a Gmail link', () => {
    const { view, of } = mount()
    const rows = view.container.querySelectorAll<HTMLElement>('.clu-mail-row')
    expect([...rows].map((r) => r.dataset.link ?? null)).toEqual(['true', null, null])
    rows.forEach((r) => fireEvent.click(r))
    expect(of('tabs.openMail')).toEqual([{ tabId: TAB, url: items[0]?.url }])
  })

  it('sends nothing without the row it belongs to', () => {
    const { view, of } = mount({ tabId: 42 })
    fireEvent.pointerEnter(view.container.querySelector('.clu-mail-list') as HTMLElement)
    view.container.querySelectorAll<HTMLElement>('.clu-mail-row').forEach((r) => fireEvent.click(r))
    expect(of('tabs.hover')).toEqual([])
    expect(of('tabs.openMail')).toEqual([])
  })

  it('reads a link as Gmail\'s only for https://mail.google.com', () => {
    expect(mailLink('https://mail.google.com/mail/u/1/#inbox/abc')).toBe('https://mail.google.com/mail/u/1/#inbox/abc')
    expect(mailLink('http://mail.google.com/')).toBeNull()
    expect(mailLink('https://mail.google.com.evil.test/')).toBeNull()
    expect(mailLink('not a url')).toBeNull()
    expect(mailLink('')).toBeNull()
  })
})
