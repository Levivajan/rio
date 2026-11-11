// Site tweaks editor (docs/plan/08-extensions.md F9 to F11): the Style tab holds the no-code controls,
// previewed in the tab on every change; CSS and JS tabs are monospace textareas, CSS previews 300 ms after the last
// keystroke; Cmd+S saves everything, Esc closes without saving. The Hidden tab lists the site's zaps (label plus
// selector) with a × each and starts zap mode.
import { useEffect, useId, useRef, useState } from 'react'
import { DEFAULT_TWEAK_CONTROLS, type TweakControls, type Zap } from '../../../shared/tweaks'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import { closeInstance, type SurfaceProps } from '../core/types'
import { StylePanel } from './StylePanel'
import './tweaks.css'
import { clu } from '../../common/clu'
import { Button, Toggle } from '../../common/controls'

export const PREVIEW_MS = 300
export const MAX_CODE_BYTES = 256 * 1024
const bytes = (s: string) => new TextEncoder().encode(s).length

type Pane = 'style' | 'css' | 'js' | 'zaps'
const PANES: { id: Pane; label: string }[] = [
  { id: 'style', label: 'Style' },
  { id: 'css', label: 'CSS' },
  { id: 'js', label: 'JS' },
  { id: 'zaps', label: 'Hidden' }
]
const sameControls = (a: TweakControls, b: TweakControls) => (Object.keys(a) as (keyof TweakControls)[]).every((k) => a[k] === b[k])

export function TweakEditor({ instance, windowId }: SurfaceProps) {
  const props = (instance.props ?? {}) as { tabId?: string; site?: string }
  const tabId = props.tabId ?? ''
  const site = props.site ?? ''
  const titleId = useId()
  const tweak = useCluState((s) => s.tabs[tabId]?.tweak ?? null)
  // The top overlay may cover only the editor's part of the window: its rect is then this document's origin.
  const topRect = useCluState((s) => s.windows[windowId]?.overlay.topRect ?? null)
  const [pane, setPane] = useState<Pane>('style')
  const [css, setCss] = useState('')
  const [js, setJs] = useState('')
  const [controls, setControls] = useState<TweakControls>(DEFAULT_TWEAK_CONTROLS)
  const [saved, setSaved] = useState<{ css: string; js: string; controls: TweakControls } | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [status, setStatus] = useState('')
  const [zaps, setZaps] = useState<Zap[]>([])
  const panel = useRef<HTMLDivElement>(null)
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null)

  useEffect(() => {
    let live = true
    void invoke(clu().tweaks.get({ tabId })).then((r) => {
      if (!live) return
      const t = r.ok ? r.value : null
      setCss(t?.css ?? '')
      setJs(t?.js ?? '')
      setControls(t?.controls ?? DEFAULT_TWEAK_CONTROLS)
      setSaved({ css: t?.css ?? '', js: t?.js ?? '', controls: t?.controls ?? DEFAULT_TWEAK_CONTROLS })
      if (!r.ok) setError('Site tweaks are not available for this page.')
    })
    return () => {
      live = false
      if (timer.current) clearTimeout(timer.current)
    }
  }, [tabId])

  // The list follows the count: zaps, undo and Restore all happen outside the editor.
  const zapCount = tweak?.zapCount ?? 0
  useEffect(() => {
    let live = true
    void invoke(clu().tweaks.get({ tabId })).then((r) => {
      if (live) setZaps(r.ok ? (r.value?.zaps ?? []) : [])
    })
    return () => {
      live = false
    }
  }, [tabId, zapCount])

  const loaded = saved !== null
  useEffect(() => {
    panel.current?.querySelector<HTMLElement>('textarea, input')?.focus()
  }, [pane, loaded])

  const tooLarge = (label: string, s: string) => (bytes(s) > MAX_CODE_BYTES ? `${label} is larger than 256 KB.` : null)

  function editCss(next: string) {
    setCss(next)
    setStatus('')
    if (timer.current) clearTimeout(timer.current)
    const big = tooLarge('CSS', next)
    setError(big)
    if (big) return
    timer.current = setTimeout(() => void invoke(clu().tweaks.previewCss({ tabId, css: next })), PREVIEW_MS)
  }

  function editControls(next: TweakControls) {
    setControls(next)
    setStatus('')
    void invoke(clu().tweaks.previewControls({ tabId, controls: next }))
  }

  async function save() {
    const big = tooLarge('CSS', css) ?? tooLarge('JavaScript', js)
    if (big) {
      setError(big)
      return
    }
    if (timer.current) clearTimeout(timer.current)
    const r = await invoke(clu().tweaks.save({ tabId, css, js, controls }))
    if (r.ok) {
      setSaved({ css, js, controls })
      setError(null)
      setStatus(js !== saved?.js && (tweak?.enabled ?? true) ? 'Saved. Page reloaded.' : 'Saved')
    } else setError(r.code === 'bad-payload' || r.code === 'too-large' ? 'This tweak is larger than 256 KB.' : 'Could not save this tweak.')
  }

  const dirty = loaded && (css !== saved.css || js !== saved.js || !sameControls(controls, saved.controls))
  const close = () => closeInstance(instance.instanceId)
  // In the top layer the editor docks at its rect, right over the page card. The browser sizes the top overlay to the
  // editor, so the page beside it takes clicks, and says so in overlay.topRect: the rect is then relative to it. A
  // passive view is already sized to it.
  const r = instance.layer === 'top' ? instance.rect : null
  const origin = topRect ?? { x: 0, y: 0 }
  const dock = r ? { position: 'fixed' as const, left: r.x - origin.x, top: r.y - origin.y, width: r.width, height: r.height } : undefined

  return (
    <div
      className="clu-tweak-editor"
      style={dock}
      role="dialog"
      aria-labelledby={titleId}
      data-tweak-editor={tabId}
      onKeyDown={(e) => {
        if (e.key === 'Escape') {
          e.preventDefault()
          close()
        } else if (e.metaKey && !e.shiftKey && !e.altKey && !e.ctrlKey && e.key.toLowerCase() === 's') {
          e.preventDefault()
          void save()
        }
      }}
    >
      <header className="clu-tweak-head">
        <div>
          <h2 id={titleId}>Site tweaks</h2>
          <div className="clu-tweak-site" title={site}>
            {site}
          </div>
        </div>
        {/* The same state as Site controls' switch, which turns on an empty tweak too. */}
        <Toggle label="Site tweaks on" checked={!!tweak?.enabled} disabled={!tweak} onChange={(on) => void invoke(clu().tweaks.setEnabled({ tabId, enabled: on }))} />
      </header>
      <div className="clu-tweak-tabs" role="tablist" aria-label="Tweak">
        {PANES.map((p) => (
          <button key={p.id} type="button" role="tab" id={`${titleId}-${p.id}`} aria-selected={pane === p.id} aria-controls={`${titleId}-panel`} onClick={() => setPane(p.id)}>
            {p.id === 'zaps' && zapCount > 0 ? `${p.label} (${zapCount})` : p.label}
          </button>
        ))}
      </div>
      <div ref={panel} className="clu-tweak-panel" role="tabpanel" id={`${titleId}-panel`} aria-labelledby={`${titleId}-${pane}`}>
      {pane === 'style' ? (
        loaded && <StylePanel value={controls} onChange={editControls} />
      ) : pane === 'zaps' ? (
        <div className="clu-tweak-zaps">
          {zaps.length === 0 ? (
            <p className="clu-tweak-hint">Nothing hidden on this site. Zap elements to click away banners, sidebars and other clutter.</p>
          ) : (
            <ul aria-label="Hidden elements">
              {zaps.map((z, i) => (
                <li key={`${i}:${z.selector}`}>
                  <div>
                    <span className="clu-tweak-zap-label">{z.label || 'Element'}</span>
                    <code title={z.selector}>{z.selector}</code>
                  </div>
                  <button type="button" aria-label={`Restore ${z.label || z.selector}`} onClick={() => void invoke(clu().tweaks.removeZap({ tabId, index: i }))}>
                    ×
                  </button>
                </li>
              ))}
            </ul>
          )}
          <Button onClick={() => void invoke(clu().tweaks.startZap({ tabId }))}>
            Zap elements
          </Button>
        </div>
      ) : (
      <textarea
        aria-label={pane === 'css' ? 'CSS' : 'JavaScript'}
        className="clu-tweak-code"
        spellCheck={false}
        autoCapitalize="off"
        autoCorrect="off"
        disabled={!loaded}
        placeholder={pane === 'css' ? 'h1 { color: rebeccapurple; }' : "document.body.dataset.theme = 'mine'"}
        value={pane === 'css' ? css : js}
        onChange={(e) => {
          if (pane === 'css') editCss(e.target.value)
          else {
            setJs(e.target.value)
            setStatus('')
            setError(tooLarge('JavaScript', e.target.value))
          }
        }}
      />
      )}
      </div>
      {pane === 'css' && <p className="clu-tweak-hint">Add !important to win over the site's own styles.</p>}
      {error && (
        <p className="clu-tweak-error" role="alert">
          {error}
        </p>
      )}
      <footer className="clu-tweak-foot">
        <span role="status">{status || (dirty ? 'Unsaved changes' : '')}</span>
        <Button onClick={close} aria-keyshortcuts="Escape">
          Close
        </Button>
        <Button className="clu-tweak-save" onClick={() => void save()} disabled={!loaded} aria-keyshortcuts="Meta+S">
          Save ⌘S
        </Button>
      </footer>
    </div>
  )
}
