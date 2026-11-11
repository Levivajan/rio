import { describe, expect, it } from 'vitest'
import { probeId } from '../../src/shared/look/compare'
import { LOOK_TARGETS } from '../../src/shared/look/targets'
import { judgeReport } from './look-compare'

describe('look-compare', () => {
  const native = LOOK_TARGETS.filter((t) => t.state === 'native')

  it('judges a browser test report with the shared comparator', () => {
    const readings: Record<string, unknown> = {}
    for (const t of native) readings[probeId(t.state, t.probe)] = 'px' in t.expect ? t.expect.px : [250, 251, 255]
    const checks = judgeReport({ state: 'native', context: { tint: [223, 221, 239] }, readings })
    expect(checks).toHaveLength(native.length)
    expect(checks.every((c) => c.pass)).toBe(true)
  })

  it('reads junk fields as missing and refuses an unknown state', () => {
    const checks = judgeReport({ state: 'native', context: { tint: 'pink', sidebarWidth: -Infinity }, readings: { x: { at: [1, 2] }, [probeId('native', { native: 'dialog-top' })]: '10' } })
    expect(checks.every((c) => !c.pass)).toBe(true)
    expect(() => judgeReport({ state: 'nope' })).toThrow(/unknown state/)
    expect(() => judgeReport(null)).toThrow(/unknown state/)
  })
})
