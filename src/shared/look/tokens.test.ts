// tokens.css holds every look value of tokens.ts, light and dark; an optional LOOK.json token file outside the repo
// must agree with tokens.ts when present.
import { existsSync, readFileSync } from 'node:fs'
import { homedir } from 'node:os'
import { resolve } from 'node:path'
import { describe, expect, it } from 'vitest'
import { LOOK_TARGETS, NOT_TARGETS } from './targets'
import { LOOK, cssColor, cssLinear, cssShadow, cssTokens } from './tokens'

const css = readFileSync(resolve(__dirname, '../../renderer/common/tokens.css'), 'utf8')

function block(selector: string): Map<string, string> {
  const start = css.indexOf(`${selector} {`)
  if (start < 0) throw new Error(`no ${selector} block`)
  const body = css.slice(start, css.indexOf('\n}', start))
  return new Map([...body.matchAll(/(--[a-z0-9-]+):\s*([^;]+);/g)].map((m) => [m[1] as string, (m[2] as string).trim()]))
}

describe('tokens.css', () => {
  it('holds every light value of tokens.ts in :root', () => {
    const root = block(':root')
    for (const [name, value] of Object.entries(cssTokens('light'))) expect([name, root.get(name)]).toEqual([name, value])
  })

  it('holds every dark color in the dark block', () => {
    const dark = block(":root[data-theme='dark']")
    for (const [name, value] of Object.entries(cssTokens('dark'))) expect([name, dark.get(name)]).toEqual([name, value])
  })

  it('fades hover reveals over the design sidebar-hover fade of 150 ms, settling from 94%', () => {
    const root = block(':root')
    expect(root.get('--t-reveal')).toBe('150ms')
    expect(root.get('--reveal-settle')).toBe('0.06')
    expect(root.get('--ease-in')).toBe('cubic-bezier(0.4, 0, 1, 1)')
  })

  it('gives reduced motion a 0 for every duration but the cross-fade', () => {
    const reduced = block(":root[data-reduced-motion='true']")
    for (const name of ['--t-fast', '--t-base', '--t-slow', '--t-press', '--t-hover-out', '--t-close-hover', '--t-hover-card-out', '--tip-out', '--t-reveal', '--t-menu-out']) expect(reduced.get(name)).toBe('0ms')
    expect(reduced.get('--t-fade')).toBe('120ms')
  })
})

describe('css helpers', () => {
  it('writes opaque colors as hex and the rest as rgb with an alpha', () => {
    expect(cssColor({ r: 0x90, g: 0x95, b: 0xfb, a: 1 })).toBe('#9095fb')
    expect(cssColor({ r: 14, g: 15, b: 16, a: 0.05 })).toBe('rgb(14 15 16 / 5%)')
    expect(cssColor({ r: 300, g: -4, b: 16.4, a: 1 })).toBe('#ff0010')
  })

  it('writes shadows and sampled curves', () => {
    expect(cssShadow(LOOK.card.shadow)).toBe('0 1.5px 7px rgb(0 0 0 / 23%)')
    expect(cssLinear([[0, 0], [50, 0.5], [100, 1]])).toBe('linear(0, 0.5 50%, 1)')
  })
})

describe('targets', () => {
  it('name a LOOK.json section and token, and never one INVENTORY.md marks as the owner\'s setting', () => {
    for (const t of LOOK_TARGETS) {
      expect(t.key).toMatch(/^(chrome|rows|spaces|command|menus|multitask|pages|motion)\.[A-Za-z]+$/)
      expect(Object.keys(NOT_TARGETS)).not.toContain(t.key)
    }
  })
})

// The optional token file: ~/.clu/look/LOOK.json, or the path in CLU_LOOK_JSON.
const lookPath = process.env.CLU_LOOK_JSON ?? resolve(homedir(), '.clu', 'look', 'LOOK.json')
describe.skipIf(!existsSync(lookPath))('LOOK.json cross-check', () => {
  type Token = { value: unknown; source: string }
  const look = existsSync(lookPath) ? (JSON.parse(readFileSync(lookPath, 'utf8')) as Record<string, Record<string, Token>>) : {}
  const token = (key: string) => {
    const [section, name] = key.split('.') as [string, string]
    return look[section]?.[name]
  }

  it('every target is a measured or estimated token', () => {
    for (const t of LOOK_TARGETS) expect([t.key, token(t.key)?.source]).toEqual([t.key, expect.stringMatching(/^(measured|estimated)$/)])
  })

  it('tokens.ts copied the numbers', () => {
    const v = (key: string) => token(key)?.value as Record<string, unknown> & number
    expect(LOOK.card.radius).toBe(v('chrome.cardRadius'))
    expect(LOOK.card.inset).toBe(v('chrome.cardInsetTop'))
    expect(LOOK.card.inset).toBe(v('chrome.cardInsetBottom'))
    expect(LOOK.window.trafficLights.diameter).toBe(v('chrome.trafficLightDiameter'))
    expect([LOOK.window.trafficLights.firstCenterX, LOOK.window.trafficLights.centerY]).toEqual((v('chrome.trafficLightCenters') as unknown as number[][])[0])
    expect(LOOK.sidebar.toolbar.pitch).toBe(v('chrome.toolbarIconPitch'))
    expect(LOOK.sidebar.row.height).toBe(v('rows.tabRowHeight'))
    expect(LOOK.sidebar.row.gap).toBe(v('rows.tabRowGap'))
    expect(LOOK.sidebar.row.height + LOOK.sidebar.row.gap).toBe(v('rows.tabRowPitch'))
    expect(LOOK.sidebar.urlPill.height).toBe(v('chrome.urlPill').h)
    expect(LOOK.sidebar.urlPill.radius).toBe(v('chrome.urlPill').radius)
    expect(LOOK.sidebar.favorites.tileHeight).toBe(v('rows.favoriteTile').h)
    expect(LOOK.sidebar.favorites.tileRadius).toBe(v('rows.favoriteTile').radius)
    expect(LOOK.sidebar.row.activeRadius).toBe(v('rows.rowActive').radius)
    expect(LOOK.command.radius).toBe(v('command.panel').radius)
    expect(LOOK.command.maxWidth).toBe(v('command.panel').w)
    expect(LOOK.command.inputHeight).toBe(v('command.inputRowHeight'))
    expect(LOOK.command.rowPitch).toBe(v('command.resultRowPitch'))
    expect(LOOK.command.maxRows).toBe(v('command.maxRows'))
    expect(LOOK.motion.spaceTintMs).toBe((v('motion.spaceSwitchTint') as unknown as { duration: number }).duration)
    expect(LOOK.motion.spaceSlideMs).toBe((v('motion.spaceSwitchSlide') as unknown as { duration: number }).duration)
    // With a page loaded the sidebar docks in 85 ms and hides in 80.
    expect([LOOK.sidebar.showMs, LOOK.sidebar.hideMs]).toEqual([85, 80])
    expect(LOOK.mini.radius).toBe(v('multitask.miniWindowPrompt').radius)
  })
})

// Sidebar row, tooltip and hover card motion timings.
describe('row and tooltip motion', () => {
  it('keeps the row and tooltip timings', () => {
    const row = LOOK.sidebar.row
    expect([row.hoverInMs, row.hoverOutMs, row.pressMs, row.pressScale, row.close.inMs]).toEqual([0, 33, 75, 0.985, 85])
    const t = LOOK.tooltip
    expect([t.delayMs, t.outMs, t.hoverCardDelayMs, t.hoverCardOutMs]).toEqual([450, 280, 1500, 90])
  })

  it('gives the browser the same hover card fade as the overlay', () => {
    const header = readFileSync(resolve(__dirname, '../../../chromium/clu/browser/ui/webui/clu_window_state.h'), 'utf8')
    expect(header).toContain(`kHoverCardOut = base::Milliseconds(${LOOK.tooltip.hoverCardOutMs})`)
  })
})
