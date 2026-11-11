// The M11 comparator: judges what a screenshot suite read (chromium/clu/test/data/look/measure.js in the frame and
// overlay WebUIs, pixels from the screenshot, native values from the browser test) against LOOK_TARGETS. The WebUI
// harness calls it directly; clu_browsertests write their readings as JSON (CluLook*), which
// `node scripts/chromium/run-ts.mjs scripts/chromium/look-compare.ts <file>` judges the same way.
import type { Rgb } from '../theme/space'
import { blend, channelDelta, roundRgb, tintOverWhite } from './derive'
import { LOOK_TARGETS, resolveTarget, type LookState, type LookTarget, type Probe } from './targets'
import { cssColor } from './tokens'

/** A reading: a number (points, ms, a ratio), a color, or null when the element was not there. */
export type LookValue = number | Rgb | null

/** What the expected colors and the width rules are computed from, read in the same state. */
export interface LookContext { tint: Rgb; panel: Rgb; sidebarWidth: number; windowHeight: number }

export interface LookCheck {
  key: string
  state: LookState
  probe: string
  expected: string
  actual: string
  tol: number
  pass: boolean
}

/** A stable id per probe, the key of a reading: `state|doc|selector|what`. */
export function probeId(state: LookState, p: Probe): string {
  if ('native' in p) return `${state}|native|${p.native}`
  const what = 'box' in p ? `box:${p.box}` : 'css' in p ? `css:${p.css}` : 'pixel' in p ? `pixel:${p.pixel.join(',')}` : `gap${p.axis ?? 'y'}:${p.gapTo}`
  return `${state}|${p.doc}|${p.sel}|${what}`
}

const isRgb = (v: LookValue): v is Rgb => Array.isArray(v) && v.length === 3 && v.every((n) => Number.isFinite(n))
const fmt = (v: LookValue) => (v === null ? 'missing' : isRgb(v) ? cssColor({ r: v[0], g: v[1], b: v[2], a: 1 }) : String(Math.round(v * 100) / 100))

function expectedOf(t: LookTarget, ctx: LookContext): number | Rgb {
  const e = t.expect
  if ('px' in e) return e.px
  if ('ms' in e) return e.ms
  if ('ratio' in e) return e.ratio
  if ('tintShare' in e) return roundRgb(tintOverWhite(ctx.tint, e.tintShare))
  const base = e.over === 'tint' ? ctx.tint : e.over === 'panel' ? ctx.panel : ([255, 255, 255] as Rgb)
  return roundRgb(blend(e.color, base))
}

/** Checks every target of `states` that has a reading; a target with no reading fails as missing. */
export function compareLook(
  readings: Readonly<Record<string, LookValue>>,
  ctx: LookContext,
  states: readonly LookState[],
  targets: readonly LookTarget[] = LOOK_TARGETS
): LookCheck[] {
  return targets
    .filter((t) => states.includes(t.state))
    .map((raw) => {
      const t = resolveTarget(raw, ctx)
      const id = probeId(t.state, t.probe)
      const actual = readings[id] ?? null
      const expected = expectedOf(t, ctx)
      let pass = false
      if (actual !== null && isRgb(actual) && Array.isArray(expected)) pass = channelDelta(actual, expected) <= t.tol
      else if (typeof actual === 'number' && typeof expected === 'number' && Number.isFinite(actual)) pass = Math.abs(actual - expected) <= t.tol
      return { key: t.key, state: t.state, probe: id, expected: fmt(expected), actual: fmt(actual), tol: t.tol, pass }
    })
}

/** One line per check, failures first, for test output and the JSON judge. */
export function formatLookReport(checks: readonly LookCheck[]): string {
  const sorted = [...checks].sort((a, b) => Number(a.pass) - Number(b.pass))
  const lines = sorted.map((c) => `${c.pass ? 'ok  ' : 'FAIL'} ${c.key} [${c.probe}] expected ${c.expected} ±${c.tol}, got ${c.actual}`)
  const failed = checks.filter((c) => !c.pass).length
  return [`${checks.length - failed}/${checks.length} look checks pass`, ...lines].join('\n')
}

/** The probes a state reads in one document, for measure.js. */
export function probesFor(state: LookState, doc: 'frame' | 'overlay', targets: readonly LookTarget[] = LOOK_TARGETS) {
  const seen = new Map<string, Exclude<Probe, { native: string }>>()
  for (const t of targets) {
    if (t.state !== state || 'native' in t.probe || t.probe.doc !== doc) continue
    seen.set(probeId(state, t.probe), t.probe)
  }
  return [...seen].map(([id, probe]) => ({ id, probe }))
}
