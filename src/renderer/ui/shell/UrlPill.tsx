// URL pill (docs/plan/01-shell.md F6): the active tab's host only (no path, no www., no
// security glyph; data-security still marks http for the browser tests), a copy button (command.copyUrl strips tracking
// parameters and shows the "Copied Current URL" toast; the button shows a ✓ for a second after) and the extensions-owned SiteControlsButton slot. The two buttons show only while
// the pointer is over the sidebar or one of them has focus, fading in and out. Clicking the host edits the URL in the command bar. Loading shows above the page card
// (CardLoading, M11), not in the pill. While a Peek is open the pill shows the peeked page's host,
// and copies nothing (the copy acts on the tab). In toolbar mode (⇧⌘D) the pill leaves the sidebar for the
// toolbar above the page card, with the whole URL when Advanced's toolbar.fullUrl is on; a right-click on either opens the native Show as Toolbar / Hide Toolbar menu.
import { fullUrlText, pillText, security } from '../../../shared/shell/urlDisplay'
import { SIDEBAR_WIDTH } from '../../../shared/shell/layout'
import { pillPlaceholder } from '../../../shared/shell/sidebarMetrics'
import { IconButton } from '../../common/IconButton'
import { useCluState, useWindowId } from '../../common/useCluState'
import { SiteControlsButton } from '../extensions/SiteControlsButton'
import { CheckIcon, CopyLinkIcon } from './NavIcons'
import { clu } from '../../common/clu'
import { useEffect, useState, type MouseEvent } from 'react'

/** How long the copy button shows its ✓ after a copy. */
export const COPIED_CHECK_MS = 1000

/** Opens the Show as Toolbar / Hide Toolbar menu at the pointer. */
export function openToolbarMenu(e: MouseEvent<HTMLElement>): void {
  e.preventDefault()
  void clu().shell.openToolbarMenu({ anchor: { x: Math.round(e.clientX), y: Math.round(e.clientY) } })
}

export function UrlPill({ placement = 'sidebar' }: { placement?: 'sidebar' | 'toolbar' }) {
  const windowId = useWindowId() ?? ''
  const toolbar = useCluState((s) => s.windows[windowId]?.shell.toolbar ?? false)
  const tabId = useCluState((s) => s.windows[windowId]?.activeTabId ?? null)
  const url = useCluState((s) => (tabId ? (s.tabs[tabId]?.url ?? '') : ''))
  const title = useCluState((s) => (tabId ? (s.tabs[tabId]?.title ?? '') : ''))
  const peek = useCluState((s) => s.windows[windowId]?.peek ?? null)
  const width = useCluState((s) => s.windows[windowId]?.shell.layout.sidebarRect.width ?? SIDEBAR_WIDTH)
  // Advanced, Show full URL when Toolbar is enabled: the toolbar's pill shows the whole URL.
  const fullUrl = useCluState((s) => s.settings.global['toolbar.fullUrl'] === true)
  const shown = peek?.url || url
  const shownTitle = peek ? peek.title : title
  const host = shown ? (placement === 'toolbar' && fullUrl ? fullUrlText(shown, shownTitle) : pillText(shown, shownTitle)) : ''
  const sec = shown ? security(shown) : 'internal'
  const [copied, setCopied] = useState(false)
  useEffect(() => {
    if (!copied) return
    const timer = setTimeout(() => setCopied(false), COPIED_CHECK_MS)
    return () => clearTimeout(timer)
  }, [copied])
  const copy = async () => {
    if (!tabId) return
    const r = await clu().command.copyUrl({ tabId })
    if (r.ok && r.value.copied) setCopied(true)
  }
  if (toolbar !== (placement === 'toolbar')) return null
  return (
    <div className="clu-url-pill" data-shell="url-pill" data-placement={placement} data-security={sec} data-peek={peek ? true : undefined} onContextMenu={openToolbarMenu}>
      <button type="button" className="clu-url-host" data-shell="url-host" data-empty={!host} onClick={() => void clu().command.open({ mode: 'edit-url' })}>
        {host ? (
          host
        ) : (
          <>
            {/* With no page the pill shows a magnifier and a placeholder that grows with the width at 228. */}
            <svg className="clu-url-search-icon" width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" aria-hidden="true">
              <circle cx="7" cy="7" r="5" />
              <path d="m10.75 10.75 3.5 3.5" />
            </svg>
            {pillPlaceholder(placement === 'toolbar' ? Number.MAX_SAFE_INTEGER : width)}
          </>
        )}
      </button>
      {host && !peek ? (
        <IconButton aria-label="Copy URL" className="clu-url-action clu-reveal" data-shell="copy-url" data-copied={copied || undefined} onClick={() => void copy()}>
          {copied ? <CheckIcon /> : <CopyLinkIcon />}
        </IconButton>
      ) : null}
      <span className="clu-url-slot clu-reveal" data-slot="site-controls">
        <SiteControlsButton />
      </span>
    </div>
  )
}
