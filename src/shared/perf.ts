// Performance mark names, including db:ready and tab:attached.

export const PERF_MARKS = [
  'main:entry',
  'app:ready',
  'db:ready',
  'window:created',
  'ui:loaded',
  'ui:interactive',
  'overlay:ready',
  'input:command-bar',
  'cmd:open:painted',
  'tab:attached'
] as const
export type PerfMarkName = (typeof PERF_MARKS)[number]

/** Names a renderer may report through `perf.mark`. */
export const RENDERER_PERF_MARKS = ['ui:interactive', 'overlay:ready', 'cmd:open:painted'] as const
export type RendererPerfMarkName = (typeof RENDERER_PERF_MARKS)[number]

export interface PerfMark { name: PerfMarkName; at: number }
