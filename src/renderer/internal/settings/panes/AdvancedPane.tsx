// Advanced (docs/chromium/parity-settings.md section 3): three cards of switches with colored tiles
// (haptic feedback on reorder; the full URL in the toolbar; picture in picture and Site tweaks everywhere), then
// appearance and the default browser, the tweaks of each profile, the import from another browser, and a row of
// buttons at the foot, whose More Settings… opens Chromium's own settings.
import { clu } from '../../../common/clu'
import { Button, Card } from '../../../common/controls'
import { ImportSection } from '../../../settings-sections/data/import'
import { SettingSelect } from '../../../settings-sections/data/parts/fields'
import { SiteTweaksSection } from '../../../settings-sections/tweaks/SiteTweaks'
import { APPEARANCE, DefaultBrowser } from './GeneralPane'
import { Heading, SwitchRow, type SwitchTile } from './parts'

const glyph = { width: 14, height: 14, viewBox: '0 0 16 16', 'aria-hidden': true } as const

// The design's SF Symbols, drawn as white glyphs: hand.tap.fill, ellipsis.rectangle, play.square, paintbrush.fill.
const TILES = {
  haptic: {
    tint: 'blue',
    glyph: (
      <svg {...glyph} fill="currentColor">
        <path d="M6.4 8.3V4a1.1 1.1 0 0 1 2.2 0v3.4l2.6.5c.9.2 1.5 1 1.4 1.9l-.4 2.6a1.9 1.9 0 0 1-1.9 1.6H8.2a2 2 0 0 1-1.6-.8L4.3 10.3a1 1 0 0 1 1.5-1.3z" />
        <path d="M4.3 5.4a3.4 3.4 0 0 1 6.4 0" fill="none" stroke="currentColor" strokeWidth="1.2" strokeLinecap="round" />
      </svg>
    )
  },
  fullUrl: {
    tint: 'red',
    glyph: (
      <svg {...glyph} fill="none" stroke="currentColor" strokeWidth="1.3">
        <rect x="2" y="4.2" width="12" height="7.6" rx="1.8" />
        <circle cx="5.4" cy="8" r=".5" fill="currentColor" />
        <circle cx="8" cy="8" r=".5" fill="currentColor" />
        <circle cx="10.6" cy="8" r=".5" fill="currentColor" />
      </svg>
    )
  },
  pip: {
    tint: 'magenta',
    glyph: (
      <svg {...glyph} fill="none" stroke="currentColor" strokeWidth="1.3">
        <rect x="2.6" y="2.6" width="10.8" height="10.8" rx="2" />
        <path d="M6.6 5.6v4.8L10.4 8z" fill="currentColor" strokeLinejoin="round" />
      </svg>
    )
  },
  tweaks: {
    tint: 'magenta',
    glyph: (
      <svg {...glyph} fill="currentColor">
        <path d="M13.7 2.3a1 1 0 0 0-1.4 0L7.2 7.4l1.4 1.4 5.1-5.1a1 1 0 0 0 0-1.4z" />
        <path d="M6.3 8.4c-1.2 0-2.1.9-2.3 2.1-.1.9-.5 1.5-1.5 1.9 1.7 1.2 4.8 1 5.4-1.2.3-1.1-.3-2.3-1.6-2.8z" />
      </svg>
    )
  }
} satisfies Record<string, SwitchTile>

export function AdvancedPane() {
  return (
    <>
      <Card>
        <SwitchRow settingKey="tabs.reorderHaptic" label="Haptic feedback when reordering tabs" tile={TILES.haptic} />
      </Card>
      <Card>
        <SwitchRow settingKey="toolbar.fullUrl" label="Show full URL when Toolbar is enabled" tile={TILES.fullUrl} />
      </Card>
      <Card>
        <SwitchRow settingKey="general.autoPictureInPicture" label="Enable Picture in Picture when you leave a video tab" tile={TILES.pip} />
        <SwitchRow settingKey="tweaks.enabled" label="Enable Site tweaks on websites you visit" tile={TILES.tweaks} />
      </Card>
      <Card>
        <SettingSelect settingKey="appearance.mode" scope="global" label="Appearance" choices={APPEARANCE} />
        <DefaultBrowser />
      </Card>
      <Heading>Site tweaks</Heading>
      <SiteTweaksSection />
      <Heading>Import</Heading>
      <ImportSection />
      <div className="clu-sw-buttons">
        <Button onClick={() => void clu().settings.openBrowser({ page: 'root' })}>More Settings…</Button>
      </div>
    </>
  )
}
