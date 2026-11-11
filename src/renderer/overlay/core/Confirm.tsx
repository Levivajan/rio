// Confirm surface: title, body, two buttons, Esc cancels, destructive styling, role=dialog.
// Each button wears its key: Cancel "esc", the confirm button
// "return", and Return confirms wherever focus sits in the dialog.
import { Dialog } from '../../common/Dialog'
import { respond, type SurfaceProps } from './types'
import { Button } from '../../common/controls'

export function ConfirmSurface({ instance }: SurfaceProps) {
  const p = (instance.props ?? {}) as { title?: string; body?: string; confirmLabel?: string; cancelLabel?: string; destructive?: boolean }
  const cancel = () => respond(instance.instanceId, false)
  return (
    <div
      className="clu-center"
      data-surface="confirm"
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={(e) => {
        if (e.key !== 'Enter' || e.nativeEvent.isComposing) return
        e.preventDefault()
        respond(instance.instanceId, true)
      }}
    >
      <Dialog title={p.title ?? 'Are you sure?'} onCancel={cancel}>
        <p className="clu-dialog-body">{p.body ?? ''}</p>
        <div className="clu-dialog-actions">
          <Button onClick={cancel}>
            {p.cancelLabel ?? 'Cancel'}
            <kbd className="clu-dialog-key" aria-hidden="true">esc</kbd>
          </Button>
          <Button variant={p.destructive ? 'danger' : 'primary'} onClick={() => respond(instance.instanceId, true)}>
            {p.confirmLabel ?? 'OK'}
            <kbd className="clu-dialog-key" aria-hidden="true">return</kbd>
          </Button>
        </div>
      </Dialog>
    </div>
  )
}
