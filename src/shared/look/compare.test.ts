import { describe, expect, it } from 'vitest'
import { compareLook, formatLookReport, probeId, probesFor, type LookContext } from './compare'
import { blend, channelDelta, inkForTint, tintOverWhite } from './derive'
import { LOOK_STATES, LOOK_TARGETS, resolveTarget, type LookTarget } from './targets'
import { LOOK } from './tokens'

const PINK: [number, number, number] = [0xe1, 0xb8, 0xb9]
const ctx: LookContext = { tint: PINK, panel: [0xf8, 0xf9, 0xf9], sidebarWidth: 260, windowHeight: 800 }

describe('derive', () => {
  it('reproduces the target colors on the pink tint from one alpha each', () => {
    // The URL pill (#d7b0b1), row hover (#d6afb1), selected row (#f4efef) and empty page (#f5e7e8) on the pink tint.
    expect(channelDelta(blend(LOOK.color.light.itemBg, PINK), [0xd7, 0xb0, 0xb1])).toBeLessThanOrEqual(2)
    expect(channelDelta(blend(LOOK.color.light.rowHover, PINK), [0xd6, 0xaf, 0xb1])).toBeLessThanOrEqual(2)
    expect(channelDelta(blend(LOOK.color.light.rowCurrent, PINK), [0xf4, 0xef, 0xef])).toBeLessThanOrEqual(2)
    expect(channelDelta(tintOverWhite(PINK, LOOK.card.emptyTintShare), [0xf5, 0xe7, 0xe8])).toBeLessThanOrEqual(2)
    // The selected row on the lavender tint (#f4f3f5 on #e1deef) from the same alpha.
    expect(channelDelta(blend(LOOK.color.light.rowCurrent, [0xe1, 0xde, 0xef]), [0xf4, 0xf3, 0xf5])).toBeLessThanOrEqual(2)
    // Glyphs: the sidebar toggle (#614f4f) and the inactive switcher dot (#a68788).
    expect(channelDelta(blend(LOOK.color.light.glyph, PINK), [0x61, 0x4f, 0x4f])).toBeLessThanOrEqual(2)
    expect(channelDelta(blend(LOOK.color.light.glyphInactive, PINK), [0xa6, 0x87, 0x88])).toBeLessThanOrEqual(2)
    // The command bar's text on its panel.
    expect(channelDelta(blend(LOOK.color.light.cmdText, ctx.panel), [0x32, 0x32, 0x32])).toBeLessThanOrEqual(2)
    expect(channelDelta(blend(LOOK.color.light.cmdHairline, ctx.panel), [0xdf, 0xe0, 0xe0])).toBeLessThanOrEqual(2)
  })

  it('pulls the ink toward the tint hue and keeps gray tints neutral', () => {
    expect(inkForTint(PINK)).toMatch(/^#3[0-9a-f]0[0-9a-f]0[0-9a-f]$/)
    expect(inkForTint([230, 230, 230])).toBe('#1f1f1f')
  })

  it('clamps junk alphas and shares', () => {
    expect(blend({ r: 0, g: 0, b: 0, a: 7 }, PINK)).toEqual([0, 0, 0])
    expect(tintOverWhite(PINK, -1)).toEqual([255, 255, 255])
  })
})

describe('compareLook', () => {
  const pass = (t: LookTarget): number | [number, number, number] => {
    const r = resolveTarget(t, ctx)
    const e = r.expect
    if ('px' in e) return e.px + r.tol / 2
    if ('ms' in e) return e.ms
    if ('ratio' in e) return e.ratio
    if ('tintShare' in e) return tintOverWhite(ctx.tint, e.tintShare)
    return blend(e.color, e.over === 'tint' ? ctx.tint : e.over === 'panel' ? ctx.panel : [255, 255, 255])
  }

  it('passes readings within tolerance and fails missing or far ones', () => {
    const readings = Object.fromEntries(LOOK_TARGETS.map((t) => [probeId(t.state, t.probe), pass(t)]))
    const checks = compareLook(readings, ctx, LOOK_STATES)
    expect(checks.filter((c) => !c.pass)).toEqual([])
    expect(checks).toHaveLength(LOOK_TARGETS.length)

    const card = LOOK_TARGETS.find((t) => t.key === 'chrome.cardRadius' && t.state === 'page-card') as LookTarget
    const off = compareLook({ ...readings, [probeId(card.state, card.probe)]: 10 }, ctx, ['page-card'])
    expect(off.find((c) => c.probe === probeId(card.state, card.probe))).toMatchObject({ pass: false, expected: '6.5', actual: '10' })
    const missing = compareLook({}, ctx, ['page-card'])
    expect(missing.every((c) => !c.pass && c.actual === 'missing')).toBe(true)
    expect(formatLookReport(off)).toMatch(/^\d+\/\d+ look checks pass\nFAIL chrome\.cardRadius/)
  })

  it('treats junk readings as failures, never throws', () => {
    const t = LOOK_TARGETS.find((x) => x.state === 'rows-rest') as LookTarget
    for (const junk of [Number.NaN, Infinity, [1, 2] as unknown as number, 'x' as unknown as number]) {
      const [c] = compareLook({ [probeId(t.state, t.probe)]: junk }, ctx, [t.state], [t])
      expect(c?.pass).toBe(false)
    }
  })

  it('resolves the width rules from the layout', () => {
    const box = LOOK_TARGETS.find((t) => t.key === 'rows.tabRowBox') as LookTarget
    expect(resolveTarget(box, ctx).expect).toEqual({ px: 244 })
    expect(resolveTarget(box, { ...ctx, sidebarWidth: 300 }).expect).toEqual({ px: 284 })
  })

  it('lists each probe of a state and document once', () => {
    const probes = probesFor('rows-rest', 'frame')
    expect(new Set(probes.map((p) => p.id)).size).toBe(probes.length)
    expect(probes.length).toBeGreaterThan(10)
    expect(probesFor('native', 'frame')).toEqual([])
  })

  it('covers every state it names', () => {
    for (const s of LOOK_STATES) expect([s, LOOK_TARGETS.some((t) => t.state === s)]).toEqual([s, true])
  })
})
