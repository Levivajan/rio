// Zoom pill (06 2.7): a passive surface at the top right of the page; main hides it after the last change.
import type { SurfaceProps } from '../core/types'
import { ZoomControls } from './ZoomControls'

export function ZoomPill({ instance }: SurfaceProps) {
  const { pageId } = instance.props as { pageId: string }
  return (
    <div className="clu-engine-float clu-zoom-pill">
      <ZoomControls tabId={pageId} />
    </div>
  )
}
