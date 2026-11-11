// General (docs/chromium/parity-settings.md section 3), two short cards: Automatically update Rio and Warn
// before quitting, then Previews and the way to the profile settings. Appearance, the default browser and the full
// import sit in Advanced; General adds only the one Import from Arc row, which opens in place, and at #import-chrome
// the Chrome import, first (Import from Google Chrome). Rio's
// previews are the Hover Previews of the AI features tab.
import { useEffect, useId, useState } from 'react'
import { clu } from '../../../common/clu'
import { Button, Card, Row } from '../../../common/controls'
import { invoke } from '../../../common/invoke'
import { useCluState } from '../../../common/useCluState'
import { ArcImport, ChromeImportSection } from '../../../settings-sections/data/import'
import { defaultBrowserRow, type DefaultBrowserAsk } from '../../../settings-sections/data/parts/defaultBrowser'
import type { SettingsTab } from '../tabs'
import { SwitchRow } from './parts'

export const APPEARANCE = [
  { value: 'auto', label: 'Match system' },
  { value: 'light', label: 'Light' },
  { value: 'dark', label: 'Dark' }
] as const

/** app.defaultBrowser and a button that asks macOS (platform.requestDefaultBrowser), as onboarding's step does. */
export function DefaultBrowser() {
  const state = useCluState((s) => s.app.defaultBrowser)
  const [ask, setAsk] = useState<DefaultBrowserAsk>(null)
  const [busy, setBusy] = useState(false)
  const id = useId()
  const row = defaultBrowserRow(state, ask)
  const request = async () => {
    setBusy(true)
    const r = await invoke(clu().platform.requestDefaultBrowser())
    setBusy(false)
    setAsk(r.ok ? r.value.status : r.disabled ? 'not-packaged' : 'failed')
  }
  return (
    <Row label="Default browser" hint={<span data-default-browser={state}>{row.text}</span>} hintId={`${id}-status`} hintRole="status">
      {row.canAsk ? (
        <Button disabled={busy} aria-describedby={`${id}-status`} onClick={() => void request()}>
          Make Default
        </Button>
      ) : null}
    </Row>
  )
}

/** The hash settings.importArc loads: General with the Import from Arc card open (clu_settings_window.cc IsTab). */
export const IMPORT_ARC_HASH = '#import-arc'
/** The hash settings.importChrome and settings.import load: General with the Chrome import first. */
export const IMPORT_CHROME_HASH = '#import-chrome'

/** True once the page's hash asked for `hash`, on load or later (the command on an open window). */
function useAskedHash(hash: string): boolean {
  const [asked, setAsked] = useState(() => location.hash === hash)
  useEffect(() => {
    const follow = () => {
      if (location.hash === hash) setAsked(true)
    }
    window.addEventListener('hashchange', follow)
    return () => window.removeEventListener('hashchange', follow)
  }, [hash])
  return asked
}

export function GeneralPane({ go }: { go: (tab: SettingsTab) => void }) {
  const importArc = useAskedHash(IMPORT_ARC_HASH)
  const importChrome = useAskedHash(IMPORT_CHROME_HASH)
  return (
    <>
      {importChrome ? <ChromeImportSection /> : null}
      <Card>
        <SwitchRow settingKey="general.autoUpdate" label="Automatically update Rio" info="Rio checks for updates every hour, downloads them and installs them when you quit. Check for Updates… in the Rio menu works either way." />
        <SwitchRow settingKey="general.warnBeforeQuit" label="Warn before quitting" />
      </Card>
      <Card>
        <Row label="Previews">
          <Button onClick={() => go('ai')}>Previews Settings…</Button>
        </Row>
        <Row label="Manage your default search engine, Auto-Archive, and downloads in Profile Settings.">
          <Button onClick={() => go('profiles')}>Profile Settings…</Button>
        </Row>
      </Card>
      <ArcImport open={importArc} />
    </>
  )
}
