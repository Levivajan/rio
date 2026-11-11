// Site Permissions popover: View > Site Permissions… shows SitePermissionsPanel for the focused
// tab in a light-dismiss popover at the top right of the page frame.
import { useEffect, useRef } from 'react'
import { originHost } from '../../../shared/capabilities'
import { useCluState } from '../../common/useCluState'
import { closeInstance, type SurfaceProps } from '../core/types'
import { SitePermissionsPanel } from './SitePermissionsPanel'

export function SitePermissionsPopover({ instance }: SurfaceProps) {
  const props = (instance.props ?? {}) as { tabId?: string; left?: number; top?: number }
  const tabId = typeof props.tabId === 'string' ? props.tabId : ''
  const url = useCluState((s) => s.tabs[tabId]?.url ?? '')
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => ref.current?.focus(), [])
  return (
    <div
      ref={ref}
      className="clu-site-permissions-popover"
      role="dialog"
      aria-label="Site permissions"
      tabIndex={-1}
      style={{ left: props.left ?? 12, top: props.top ?? 12 }}
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={(e) => {
        if (e.key === 'Escape') closeInstance(instance.instanceId)
      }}
    >
      <h2>{originHost(url)}</h2>
      <SitePermissionsPanel tabId={tabId} />
    </div>
  )
}
