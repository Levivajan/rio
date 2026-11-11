// "Leave site?" (06 2.4, PLAN decision 28). Stay comes first, so the keyboard's default keeps the page.
import { Dialog } from '../../common/Dialog'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function LeaveSitePrompt({ instance }: SurfaceProps) {
  const { promptId } = instance.props as { promptId: string }
  const answer = (action: 'stay' | 'leave') => void clu().prompt.respond({ promptId, action })
  return (
    <div className="clu-center clu-prompt" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="Leave site?" onCancel={() => answer('stay')}>
        <p className="clu-prompt-text">Changes you made may not be saved.</p>
        <div className="clu-prompt-actions">
          <Button onClick={() => answer('stay')}>
            Stay
          </Button>
          <Button variant="danger" onClick={() => answer('leave')}>
            Leave
          </Button>
        </div>
      </Dialog>
    </div>
  )
}
