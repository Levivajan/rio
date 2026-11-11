// The URL pill's insecure state for a page reached through a certificate bypass (PLAN decision 10). The pill belongs to
// the shell module and judges a URL by its scheme alone, so this marks it from outside; engine.css draws "Not secure".
import { useEffect } from 'react'
import { useCluState, useWindowId } from '../../common/useCluState'
import './engine.css'

export function UrlPillBypass({ tabId }: { tabId: string }) {
  const windowId = useWindowId() ?? ''
  // The window has one pill, which shows the active tab: a bypassed pane beside it must not mark it.
  const bypassed = useCluState((s) => s.windows[windowId]?.activeTabId === tabId && s.tabs[tabId]?.security === 'bypassed')
  useEffect(() => {
    if (!bypassed) return
    const pill = document.querySelector('.clu-url-pill')
    pill?.setAttribute('data-bypassed', '')
    return () => pill?.removeAttribute('data-bypassed')
  }, [bypassed])
  return null
}
