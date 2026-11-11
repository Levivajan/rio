// Contract for the quality module.
import { finite, z } from '../schema'
import { RENDERER_PERF_MARKS } from '../perf'
import { defineCommand } from './define'

export const qualityIpc = {
  'perf.mark': defineCommand({ input: z.object({ name: z.enum(RENDERER_PERF_MARKS), at: finite() }).strict(), allow: ['ui', 'overlay'] })
} as const
