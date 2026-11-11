// markAfterPaint: two frames after a commit, then perf.mark.
import type { RendererPerfMarkName } from '../../shared/perf'
import { clu } from './clu'

export function markAfterPaint(name: RendererPerfMarkName): void {
  requestAnimationFrame(() =>
    requestAnimationFrame(() => {
      void clu().perf.mark({ name, at: performance.timeOrigin + performance.now() })
    })
  )
}
