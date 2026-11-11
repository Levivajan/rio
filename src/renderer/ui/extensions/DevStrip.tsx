// Developer Mode strip: a site in Developer Mode (the Developer Mode row in Site controls;
// on by default for localhost and 127.0.0.1) shows this band above the page card instead of the toolbar's pill. On a
// diagonally striped tint of the space color: the lock and the full URL (port, path and query) in a monospace font at
// the left; at the right, red line icons in groups: copy link | screenshot | console, DevTools, inspect element, hard
// reload | extensions, sidebar.
import type { MouseEvent, ReactNode } from 'react'
import { siteControlsPage } from '../../../shared/state/extensions'
import { clu } from '../../common/clu'
import { IconButton } from '../../common/IconButton'
import { invoke } from '../../common/invoke'
import { useCluState, useWindowId } from '../../common/useCluState'
import './devStrip.css'

type Tool = 'console' | 'devtools' | 'inspect' | 'hardReload'

const Icon = ({ d }: { d: string }) => (
  <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.3" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d={d} />
  </svg>
)
const LINK = 'M6.8 9.2a2.6 2.6 0 0 0 3.7 0l2.2-2.2a2.6 2.6 0 0 0-3.7-3.7l-.9.9M9.2 6.8a2.6 2.6 0 0 0-3.7 0L3.3 9a2.6 2.6 0 0 0 3.7 3.7l.9-.9'
const CAMERA = 'M2 5.2h2.7L6 3.5h4l1.3 1.7H14v7.8H2zM8 6.6a2.2 2.2 0 1 0 0 4.4 2.2 2.2 0 0 0 0-4.4'
const CONSOLE = 'M2 2.5h12v11H2zM4.5 6l2 2-2 2M8 10.5h3.5'
const GLOBE = 'M8 2a6 6 0 1 0 0 12A6 6 0 0 0 8 2zM2 8h12M8 2c1.7 1.6 2.5 3.6 2.5 6S9.7 12.4 8 14M8 2C6.3 3.6 5.5 5.6 5.5 8s.8 4.4 2.5 6'
const CROSSHAIR = 'M8 2.5a5.5 5.5 0 1 0 0 11 5.5 5.5 0 0 0 0-11zM8 1v3M8 12v3M1 8h3M12 8h3'
const HARD_RELOAD = 'M13 8a5 5 0 1 1-1.5-3.6M13 2.5v2.8h-2.8M8 5.5v3l2 1'
const PUZZLE = 'M3 5.5h2.5a1.5 1.5 0 1 1 3 0H11v2.5a1.5 1.5 0 1 1 0 3V13H3z'
const SIDEBAR = 'M2 3h12v10H2zM6 3v10'
const LOCK = 'M5 7V5.3a3 3 0 0 1 6 0V7M3.8 7h8.4v6.5H3.8z'
const OPEN_LOCK = 'M5 7V5.3a3 3 0 0 1 5.6-1.5M3.8 7h8.4v6.5H3.8z'

const rectOf = (e: MouseEvent<HTMLElement>) => {
  const r = e.currentTarget.getBoundingClientRect()
  return { x: r.x, y: r.y, width: r.width, height: r.height }
}

const Group = ({ children }: { children: ReactNode }) => <div className="clu-dev-strip-group">{children}</div>

/** The front page when its site is in Developer Mode, else null. */
export function useDevStripPage(windowId: string) {
  const id = useCluState((s) => {
    const page = s.windows[windowId]?.mini ? null : siteControlsPage(s, windowId)
    return page?.site && page.devMode ? page.id : null
  })
  const url = useCluState((s) => (id ? s.tabs[id]?.url : undefined))
  const security = useCluState((s) => (id ? s.tabs[id]?.security : undefined))
  return id ? { id, url: url ?? '', secure: security === 'secure' || (security === undefined && !!url?.startsWith('https:')) } : null
}

export function DevStrip({ rect }: { rect: { x: number; y: number; width: number } }) {
  const windowId = useWindowId() ?? ''
  const page = useDevStripPage(windowId)
  if (!page) return null
  const tool = (t: Tool) => void invoke(clu().siteControls.devTool({ tool: t }))
  return (
    <div className="clu-toolbar clu-dev-strip" data-shell="dev-strip" role="toolbar" aria-label="Developer Mode" style={{ left: rect.x, width: rect.width, height: rect.y }}>
      <div className="clu-dev-strip-url" title={page.url}>
        <span className="clu-dev-strip-lock" data-secure={page.secure || undefined}>
          <Icon d={page.secure ? LOCK : OPEN_LOCK} />
        </span>
        <span className="clu-dev-strip-text" data-dev-url>
          {page.url.slice(0, 2048)}
        </span>
      </div>
      <div className="clu-dev-strip-tools">
        <Group>
          <IconButton aria-label="Copy link" onClick={() => void invoke(clu().commands.run({ id: 'edit.copyUrl' }))}>
            <Icon d={LINK} />
          </IconButton>
        </Group>
        <Group>
          <IconButton aria-label="Screenshot" onClick={() => void invoke(clu().siteControls.screenshot())}>
            <Icon d={CAMERA} />
          </IconButton>
        </Group>
        <Group>
          <IconButton aria-label="Console" data-dev-tool="console" onClick={() => tool('console')}>
            <Icon d={CONSOLE} />
          </IconButton>
          <IconButton aria-label="Developer tools" data-dev-tool="devtools" onClick={() => tool('devtools')}>
            <Icon d={GLOBE} />
          </IconButton>
          <IconButton aria-label="Inspect element" data-dev-tool="inspect" onClick={() => tool('inspect')}>
            <Icon d={CROSSHAIR} />
          </IconButton>
          <IconButton aria-label="Hard reload" data-dev-tool="hardReload" onClick={() => tool('hardReload')}>
            <Icon d={HARD_RELOAD} />
          </IconButton>
        </Group>
        <Group>
          <IconButton aria-label="Extensions" onClick={(e) => void invoke(clu().extensions.openMenu({ anchor: rectOf(e) }))}>
            <Icon d={PUZZLE} />
          </IconButton>
          <IconButton aria-label="Toggle sidebar" onClick={() => void invoke(clu().shell.toggleSidebar())}>
            <Icon d={SIDEBAR} />
          </IconButton>
        </Group>
      </div>
    </div>
  )
}
