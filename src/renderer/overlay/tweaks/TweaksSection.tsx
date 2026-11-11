// Site tweaks section of the Site controls popover (docs/plan/08-extensions.md): the site's tweak
// state, an on/off switch (on for a site without a tweak yet starts an empty one), "Edit CSS & JS", "Zap elements" and, once something is
// hidden, "<k> hidden elements · Restore all".
import { siteKey } from '../../../shared/site'
import { tweakParts } from '../../../shared/tweaks'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import './tweaks.css'
import { clu } from '../../common/clu'
import { Button, Toggle } from '../../common/controls'

export function describeTweak(t: { enabled: boolean; hasCss: boolean; hasJs: boolean; hasControls: boolean; zapCount: number } | null, site: string): string {
  const parts = t ? tweakParts(t) : []
  if (t?.enabled && parts.length === 0) return `On for ${site}`
  if (!t || parts.length === 0) return `No tweaks for ${site}`
  return `${t.enabled ? 'On' : 'Off'} · ${parts.join(', ')}`
}

export function TweaksSection({ tabId }: { tabId: string }) {
  const url = useCluState((s) => s.tabs[tabId]?.url ?? '')
  const tweak = useCluState((s) => s.tabs[tabId]?.tweak ?? null)
  const site = siteKey(url)
  if (!site) return null
  const text = describeTweak(tweak, site)
  const empty = text.startsWith('No tweaks')
  const zapCount = tweak?.zapCount ?? 0
  return (
    <section className="clu-tweaks-section" aria-label="Site tweaks" data-tweaks-section={tabId} data-state={tweak?.enabled ? 'on' : empty ? 'empty' : 'off'}>
      <div>
        <div>
          <h3>Site tweaks</h3>
          <p>{text}</p>
        </div>
        <Toggle
          label="Site tweaks on"
          checked={!!tweak?.enabled}
          // The switch only switches; the browser starts an empty tweak for a site without one.
          onChange={(on) => void invoke(clu().tweaks.setEnabled({ tabId, enabled: on }))}
        />
      </div>
      <div className="clu-tweaks-actions">
        <Button onClick={() => void invoke(clu().tweaks.openEditor({ tabId }))}>
          Edit CSS & JS
        </Button>
        <Button onClick={() => void invoke(clu().tweaks.startZap({ tabId }))}>
          Zap elements
        </Button>
      </div>
      {zapCount > 0 && (
        <div className="clu-tweaks-zaps">
          <span>
            {zapCount} hidden element{zapCount === 1 ? '' : 's'}
          </span>
          <span aria-hidden="true">·</span>
          <button type="button" className="clu-link-button" onClick={() => void invoke(clu().tweaks.clearZaps({ tabId }))}>
            Restore all
          </button>
        </div>
      )}
    </section>
  )
}
