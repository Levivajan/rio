// Contract for the tweaks module.
import { id, int, str, z } from '../schema'
import { tweakControls, type SiteTweak, type SiteTweakRow } from '../tweaks'
import { defineCommand, out } from './define'

// Characters; main checks the 256 KiB byte cap per field (src/main/tweaks/validate.ts) and the envelope caps the total.
const MAX_CODE = 256 * 1024
// A stored site is whatever siteKey made of a URL (it accepts 32 KiB), so a junk long row can still be toggled and deleted.
const site = () => str(32 * 1024)

export const tweaksIpc = {
  'tweaks.get': defineCommand({ input: z.object({ tabId: id() }).strict(), output: out<SiteTweak | null>() }),
  'tweaks.save': defineCommand({ input: z.object({ tabId: id(), css: str(MAX_CODE), js: str(MAX_CODE), controls: tweakControls().optional() }).strict() }),
  'tweaks.previewCss': defineCommand({ input: z.object({ tabId: id(), css: str(MAX_CODE) }).strict() }),
  'tweaks.previewControls': defineCommand({ input: z.object({ tabId: id(), controls: tweakControls() }).strict() }),
  'tweaks.setEnabled': defineCommand({ input: z.object({ tabId: id(), enabled: z.boolean() }).strict() }),
  'tweaks.openEditor': defineCommand({ input: z.object({ tabId: id().optional() }).strict() }),
  'tweaks.startZap': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tweaks.stopZap': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tweaks.undoZap': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tweaks.removeZap': defineCommand({ input: z.object({ tabId: id(), index: int(0, 10_000) }).strict() }),
  'tweaks.clearZaps': defineCommand({ input: z.object({ tabId: id() }).strict() }),
  'tweaks.list': defineCommand({
    input: z.object({ profileId: id() }).strict(),
    output: out<SiteTweakRow[]>(),
    // The Library's Site tweaks section lists them too; toggling and deleting stay in Settings.
    allow: ['internal:settings', 'ui']
  }),
  'tweaks.setEnabledForSite': defineCommand({ input: z.object({ profileId: id(), site: site(), enabled: z.boolean() }).strict(), allow: ['internal:settings'] }),
  'tweaks.deleteForSite': defineCommand({ input: z.object({ profileId: id(), site: site() }).strict(), allow: ['internal:settings'] })
} as const
