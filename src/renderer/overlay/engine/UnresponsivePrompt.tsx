// Hung page prompt (06 2.13): a passive surface at the top right of the page. Main closes it on `responsive`.
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function UnresponsivePrompt({ instance }: SurfaceProps) {
  const { pageId, host } = instance.props as { pageId: string; host: string }
  const act = (action: 'wait' | 'reload') => void clu().issue.act({ tabId: pageId, action })
  return (
    <div role="alert" className="clu-engine-float clu-unresponsive">
      <span className="clu-unresponsive-text">{host ? `${host} isn't responding` : "This page isn't responding"}</span>
      <Button onClick={() => act('wait')}>
        Wait
      </Button>
      <Button variant="primary" onClick={() => act('reload')}>
        Reload
      </Button>
    </div>
  )
}
