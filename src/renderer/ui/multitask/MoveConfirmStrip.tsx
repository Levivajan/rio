// The Mini window's confirm strip (05 Moving to a space): when the target space uses another profile, the toolbar
// grows this strip and the page moves down under it. Move opens the URL fresh in that profile; Cancel keeps the page.
import { MINI_CONFIRM_H, MINI_TOOLBAR_H, miniConfirmText } from '../../../shared/multitask'
import type { MiniWindowState } from '../../../shared/state/multitask'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function MoveConfirmStrip({ mini }: { mini: MiniWindowState }) {
  return (
    <div
      role="alertdialog"
      aria-label={`Open in ${mini.targetSpaceName}`}
      data-mini-confirm
      className="absolute right-3 left-2 flex items-center gap-2 [-webkit-app-region:no-drag]"
      style={{ top: MINI_TOOLBAR_H, height: MINI_CONFIRM_H - 8, color: 'var(--ink)', fontSize: 'var(--fs-2)' }}
    >
      <p className="m-0 min-w-0 flex-1 truncate pl-1" style={{ color: 'var(--ink-2)' }}>
        {miniConfirmText(mini.targetSpaceName)}
      </p>
      <Button onClick={() => void clu().mini.cancelMove()}>
        Cancel
      </Button>
      <Button
        autoFocus
        variant="primary"
        onClick={() => void clu().mini.moveToSpace({ spaceId: mini.targetSpaceId, confirmProfileChange: true })}
      >
        Move
      </Button>
    </div>
  )
}
