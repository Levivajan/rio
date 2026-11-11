// Judges the look reports clu_browsertests CluLook* write ($CLU_SHOTS_DIR/chromium-m11/native-<state>.json) with the
// comparator the WebUI harness uses (src/shared/look/compare.ts), so the C++ judge and the TS one cannot drift:
//   node scripts/chromium/run-ts.mjs scripts/chromium/look-compare.ts <report.json>...
// Exits 1 when a check fails or a report is unreadable.
import { readFileSync } from 'node:fs'
import { compareLook, formatLookReport, type LookCheck, type LookContext, type LookValue } from '../../src/shared/look/compare'
import { LOOK_STATES, type LookState } from '../../src/shared/look/targets'
import { SIDEBAR_WIDTH } from '../../src/shared/shell/layout'

const isRgb = (v: unknown): v is [number, number, number] => Array.isArray(v) && v.length === 3 && v.every((n) => typeof n === 'number' && Number.isFinite(n))
const num = (v: unknown, fallback: number) => (typeof v === 'number' && Number.isFinite(v) ? v : fallback)

/** One report's checks; junk fields read as missing, never throw. Targets the browser cannot show are skipped. */
export function judgeReport(json: unknown, skip: readonly string[] = ['spaces.switcherPitch']): LookCheck[] {
  const r = (json ?? {}) as { state?: unknown; context?: Record<string, unknown>; readings?: Record<string, unknown> }
  const state = LOOK_STATES.find((s) => s === r.state) as LookState | undefined
  if (!state) throw new Error(`unknown state ${JSON.stringify(r.state)}`)
  const c = r.context ?? {}
  const ctx: LookContext = {
    tint: isRgb(c.tint) ? c.tint : [255, 255, 255],
    panel: isRgb(c.panel) ? c.panel : [248, 249, 249],
    sidebarWidth: num(c.sidebarWidth, SIDEBAR_WIDTH),
    windowHeight: num(c.windowHeight, 800)
  }
  const readings: Record<string, LookValue> = {}
  for (const [id, v] of Object.entries(r.readings ?? {})) readings[id] = typeof v === 'number' || isRgb(v) ? v : null
  return compareLook(readings, ctx, [state]).filter((check) => !skip.includes(check.key))
}

export function main(args: string[]): number {
  if (!args.length) {
    console.error('usage: look-compare.ts <report.json>...')
    return 2
  }
  let failed = 0
  for (const file of args) {
    try {
      const checks = judgeReport(JSON.parse(readFileSync(file, 'utf8')))
      console.log(`${file}\n${formatLookReport(checks)}`)
      failed += checks.filter((c) => !c.pass).length
    } catch (e) {
      console.error(`${file}: ${(e as Error).message}`)
      failed++
    }
  }
  return failed ? 1 : 0
}
