// Onboarding steps (docs/plan/01-shell.md F12 with PLAN's import and default-browser steps).
import type { CallResult } from '../../../common/invoke'
import { PICKER_SIZE } from '../../../../shared/theme/picker'

export const STEPS = ['welcome', 'look', 'moves', 'default-browser', 'import', 'done'] as const
export type Step = (typeof STEPS)[number]

/**
 * Setup asks once to bring everything over. When the first-run switch screen was shown (imported or declined)
 * the Import step drops out of the order, both ways; without it the Import step is the one ask.
 */
export const stepsFor = (switched: boolean): readonly Step[] => (switched ? STEPS.filter((s) => s !== 'import') : STEPS)

const move = (s: Step, d: 1 | -1, switched: boolean): Step => {
  const steps = stepsFor(switched)
  const i = steps.indexOf(s)
  return steps[Math.min(Math.max((i < 0 ? STEPS.indexOf(s) : i) + d, 0), steps.length - 1)] as Step
}
export const nextStep = (s: Step, switched = false): Step => move(s, 1, switched)
export const prevStep = (s: Step, switched = false): Step => move(s, -1, switched)

/** Cmd+S with no other modifier, the sidebar chord the moves step teaches. */
export function isSidebarChord(e: Pick<KeyboardEvent, 'code' | 'key' | 'metaKey' | 'ctrlKey' | 'altKey' | 'shiftKey'>): boolean {
  return e.metaKey && !e.ctrlKey && !e.altKey && !e.shiftKey && (e.code === 'KeyS' || e.key.toLowerCase() === 's')
}

export type DefaultBrowserOutcome = 'advance' | 'unavailable'

/**
 * platform.requestDefaultBrowser: macOS asks the user itself after 'requested', so the step moves on; an unpackaged
 * build ('not-packaged'), a missing handler or any error leaves only Skip.
 */
export function defaultBrowserOutcome(r: CallResult<{ status: string }>): DefaultBrowserOutcome {
  if (!r.ok) return 'unavailable'
  return r.value.status === 'requested' || r.value.status === 'already-default' ? 'advance' : 'unavailable'
}

/** The zoom that fits the whole theme editor (356 x 508) in the look step's slot: never larger than 1, and junk or an
 * unlaid slot (0 or less) keeps it at 1 so the editor still shows. */
export function lookZoom(width: number, height: number): number {
  if (!(width > 0) || !(height > 0)) return 1
  return Math.floor(Math.min(1, width / PICKER_SIZE.width, height / PICKER_SIZE.height) * 1000) / 1000
}
