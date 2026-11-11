// WebUI bridge boot for the Chromium build (`@clu/webui-boot` in scripts/chromium/webui-vite.mjs): load the generated
// Mojo bindings from this WebUI's origin and install a MojoBridge. A failure leaves the document empty and logged.
import '../common/bridge/zod-jitless'
import { installBridge } from '../common/clu'
import { createMojoBridge } from '../common/bridge/mojo'
import { loadWebUiBindings } from '../common/bridge/mojo-bindings'
import { MOJO_MODULES } from '../common/bridge/mojo-wire.gen'

export type WebUiSurface = 'frame' | 'overlay' | 'settings'

export async function bootBridge(_surface: WebUiSurface): Promise<void> {
  installBridge(await createMojoBridge(await loadWebUiBindings(MOJO_MODULES)))
}
