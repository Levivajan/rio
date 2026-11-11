// The app settings window (docs/chromium/parity-settings.md): the row of icon tabs under the window's title, and the
// selected tab's pane. The browser shows this document in a window of its own; the window's title is the page title,
// so it reads the tab's name, and it reopens on the last tab used. Each pane renders inside an error boundary, so one that throws shows an
// inline error and the tabs stay usable. The hash picks the tab (settings.open loads `#<tab>`).
import { Component, useCallback, useEffect, useState, type ErrorInfo, type ReactNode } from 'react'
import { Button, Card, Row } from '../../common/controls'
import { AdvancedPane } from './panes/AdvancedPane'
import { AiPane } from './panes/AiPane'
import { GeneralPane } from './panes/GeneralPane'
import { IconPane } from './panes/IconPane'
import { LinksPane } from './panes/LinksPane'
import { ProfilesPane } from './panes/ProfilesPane'
import { ShortcutsPane } from './panes/ShortcutsPane'
import { SETTINGS_TABS, TAB_ICONS, TAB_LABELS, tabFromHash, type SettingsTab } from './tabs'

class PaneBoundary extends Component<{ tab: SettingsTab; children: ReactNode }, { failed: boolean }> {
  override state = { failed: false }
  static getDerivedStateFromError(): { failed: boolean } {
    return { failed: true }
  }
  override componentDidCatch(error: unknown, info: ErrorInfo): void {
    console.error(`settings tab "${this.props.tab}" failed`, error, info.componentStack)
  }
  override componentDidUpdate(prev: { tab: SettingsTab }): void {
    if (prev.tab !== this.props.tab && this.state.failed) this.setState({ failed: false })
  }
  override render(): ReactNode {
    if (!this.state.failed) return this.props.children
    return (
      <Card role="alert">
        <Row label="This tab couldn’t be shown.">
          <Button onClick={() => this.setState({ failed: false })}>Try again</Button>
        </Row>
      </Card>
    )
  }
}

function Pane({ tab, go }: { tab: SettingsTab; go: (tab: SettingsTab) => void }) {
  switch (tab) {
    case 'general':
      return <GeneralPane go={go} />
    case 'profiles':
      return <ProfilesPane />
    case 'ai':
      return <AiPane />
    case 'links':
      return <LinksPane />
    case 'shortcuts':
      return <ShortcutsPane />
    case 'icon':
      return <IconPane />
    case 'advanced':
      return <AdvancedPane />
  }
}

const LAST_TAB = 'clu.settings.lastTab'

/** The tab to show: the hash settings.open asked for, else the last tab used. */
export function initialTab(hash: string, last: string | null): SettingsTab {
  return hash.replace(/^#/, '') ? tabFromHash(hash) : tabFromHash(last ?? '')
}

function lastTab(): string | null {
  try {
    return localStorage.getItem(LAST_TAB)
  } catch {
    return null
  }
}

export function SettingsWindow() {
  const [tab, setTab] = useState<SettingsTab>(() => initialTab(location.hash, lastTab()))

  useEffect(() => {
    const follow = () => setTab(tabFromHash(location.hash))
    window.addEventListener('hashchange', follow)
    return () => window.removeEventListener('hashchange', follow)
  }, [])
  useEffect(() => {
    document.title = TAB_LABELS[tab]
    try {
      localStorage.setItem(LAST_TAB, tab)
    } catch {
      // Storage refused: the window opens on General next time.
    }
  }, [tab])

  const go = useCallback((next: SettingsTab) => {
    // replaceState: switching tabs is not history (the window has no Back), and it keeps the hash for a reload.
    history.replaceState(null, '', `#${next}`)
    setTab(next)
  }, [])

  // Arrow keys move along the toolbar as in a macOS tab row.
  const onToolbarKey = (e: React.KeyboardEvent) => {
    const i = SETTINGS_TABS.indexOf(tab)
    const next = e.key === 'ArrowRight' ? SETTINGS_TABS[i + 1] : e.key === 'ArrowLeft' ? SETTINGS_TABS[i - 1] : undefined
    if (!next) return
    e.preventDefault()
    go(next)
    document.getElementById(`clu-sw-tab-${next}`)?.focus()
  }

  return (
    <div className="clu-sw" data-tab={tab}>
      <div className="clu-sw-toolbar" role="tablist" aria-label="Settings" onKeyDown={onToolbarKey}>
        {SETTINGS_TABS.map((id) => (
          <button
            key={id}
            id={`clu-sw-tab-${id}`}
            type="button"
            role="tab"
            className="clu-sw-tab"
            aria-selected={id === tab}
            aria-controls="clu-sw-pane"
            tabIndex={id === tab ? 0 : -1}
            onClick={() => go(id)}
          >
            {TAB_ICONS[id]}
            <span>{TAB_LABELS[id]}</span>
          </button>
        ))}
      </div>
      <main id="clu-sw-pane" className="clu-sw-body" role="tabpanel" aria-labelledby={`clu-sw-tab-${tab}`} data-pane={tab}>
        <PaneBoundary tab={tab}>
          <Pane tab={tab} go={go} />
        </PaneBoundary>
      </main>
    </div>
  )
}
