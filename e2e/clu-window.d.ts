// window.clu in Clu's own pages, as tests see it through page.evaluate.
import type { CluBridge } from '../src/shared/bridge'

declare global {
  interface Window {
    clu: CluBridge
  }
}
