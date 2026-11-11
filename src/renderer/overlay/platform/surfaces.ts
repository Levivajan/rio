// Overlay surfaces of the platform module. Loading this module also starts the overlay's CSP violation reporter.
import { reportCspViolations } from '../../ui/platform/cspReporter'
import type { SurfaceRegistry } from '../core/types'
import { ExternalAppPrompt } from './ExternalAppPrompt'
import { RestoredNotice } from './RestoredNotice'

reportCspViolations()

export const surfaces: SurfaceRegistry = {
  'external-app': ExternalAppPrompt,
  'crash-restored': RestoredNotice
}
