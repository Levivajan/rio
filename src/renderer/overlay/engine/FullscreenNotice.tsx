// Fullscreen notice (06 2.14): passive, top center, closed by main after 3 s or when fullscreen ends.
import type { SurfaceProps } from '../core/types'

export function FullscreenNotice({ instance }: SurfaceProps) {
  const { host } = instance.props as { host: string }
  return (
    <div className="clu-fullscreen-notice">
      <div role="status" className="clu-engine-float">
        {host ? `${host} is full screen.` : 'This page is full screen.'} Press Esc to exit
      </div>
    </div>
  )
}
