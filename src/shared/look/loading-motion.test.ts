// The page loading indicator's CSS runs on LOOK.loading's timings and moves only transform and opacity.
import { readFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { describe, expect, it } from 'vitest'
import { LOOK } from './tokens'

const shellCss = readFileSync(resolve(__dirname, '../../renderer/ui/shell/shell.css'), 'utf8')

/** The duration of the animation in `selector`'s rule in shell.css, in ms. */
function cssMs(selector: string): number {
  const at = shellCss.indexOf(`${selector} {`)
  const rule = shellCss.slice(at, shellCss.indexOf('}', at))
  const m = /animation:\s*[\w-]+\s+(\d+)ms/.exec(rule)
  return m ? Number(m[1]) : Number.NaN
}

describe('loading motion', () => {
  it('runs on the design timings, transform and opacity only', () => {
    expect(LOOK.loading).toMatchObject({ pulseMs: 1600, sweepMs: 1117, openMs: 320, doneMs: 370, doneScale: 0.25 })
    expect(cssMs(".clu-card-loading[data-phase='waiting']")).toBe(LOOK.loading.pulseMs)
    expect(cssMs(".clu-card-loading[data-phase='receiving']")).toBe(LOOK.loading.openMs)
    expect(cssMs(".clu-card-loading[data-phase='receiving']::after")).toBe(LOOK.loading.sweepMs)
    expect(cssMs(".clu-card-loading[data-phase='done']")).toBe(LOOK.loading.doneMs)
    expect(shellCss).toContain(`linear ${LOOK.loading.openMs}ms infinite backwards`)
    expect(shellCss).toContain(`scaleX(${LOOK.loading.doneScale})`)
    // No layout property moves in the indicator's keyframes.
    for (const name of ['clu-loading-pulse', 'clu-loading-open', 'clu-loading-sweep', 'clu-loading-done', 'clu-loading-done-pill']) {
      const at = shellCss.indexOf(`@keyframes ${name} {`)
      expect(at).toBeGreaterThan(-1)
      const body = shellCss.slice(at, shellCss.indexOf('\n}', at))
      expect(body).not.toMatch(/\b(left|right|width|height|top|margin)\s*:/)
    }
  })

})
