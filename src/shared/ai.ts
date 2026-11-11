// The app settings AI tab's keys (docs/chromium/parity-settings.md section 1, owned and registered by the settings
// window; docs/chromium/parity-command.md). The features only read them. Every switch is on unless it is stored
// as false, which is what the registry's boolean validator and its true default resolve junk to.
import type { Json } from './types'

export const AI = {
  askRow: 'ai.askRow',
  askAssistant: 'ai.askAssistant',
  instantLinks: 'ai.instantLinks',
  hoverPreviews: 'ai.hoverPreviews',
  tidyTitles: 'ai.tidyTitles',
  tidyDownloads: 'ai.tidyDownloads',
  tidyTabs: 'ai.tidyTabs'
} as const
export type AiSwitch = Exclude<(typeof AI)[keyof typeof AI], 'ai.askAssistant'>

export function aiOn(global: Readonly<Record<string, Json | undefined>> | undefined, key: AiSwitch): boolean {
  return global?.[key] !== false
}
