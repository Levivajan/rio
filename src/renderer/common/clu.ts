// The one door to the CluBridge. Every renderer call goes through clu(); nothing else reads window.clu.
// Chromium WebUI: the entry installs a MojoBridge (common/bridge/mojo.ts) before the app module loads. The ui:fake
// harness installs a FakeBridge (common/bridge/fake.ts). Unit tests set window.clu to a stub (test-utils/stubClu.ts).
import type { CluBridge } from '../../shared/bridge'

declare global {
  interface Window {
    clu: CluBridge
  }
}

let installed: CluBridge | null = null

/** Installs the bridge for this document, before any module that reads clu() is imported. null goes back to window.clu. */
export function installBridge(bridge: CluBridge | null): void {
  installed = bridge
}

export function clu(): CluBridge {
  return installed ?? window.clu
}
