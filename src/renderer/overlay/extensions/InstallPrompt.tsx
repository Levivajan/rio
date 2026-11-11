// Install prompt (docs/plan/08-extensions.md F3): the store's install waits on this answer. Cancel is
// the first focus, so Enter and Esc both say no.
import { Dialog } from '../../common/Dialog'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import type { SurfaceProps } from '../core/types'
import './installPrompt.css'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function InstallPrompt({ windowId }: SurfaceProps) {
  const prompt = useCluState((s) => s.windows[windowId]?.installPrompt ?? null)
  if (!prompt) return null
  const answer = (allow: boolean) => void invoke(clu().extensions.respondInstall({ requestId: prompt.requestId, allow }))
  return (
    <div className="clu-center clu-install-prompt" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title={`Add “${prompt.name}” to Rio?`} onCancel={() => answer(false)}>
        {prompt.iconUrl ? <img className="clu-install-prompt-icon" src={prompt.iconUrl} alt="" width={48} height={48} /> : null}
        {prompt.permissions.length > 0 ? (
          <>
            <p className="clu-install-prompt-lead">It can:</p>
            <ul className="clu-install-prompt-permissions" data-install-permissions>
              {prompt.permissions.map((line) => (
                <li key={line}>{line}</li>
              ))}
            </ul>
          </>
        ) : null}
        {prompt.warnUnsupported ? <p className="clu-install-prompt-warning" role="note">Some features of this extension may not work in Rio.</p> : null}
        <div className="clu-install-prompt-actions">
          <Button onClick={() => answer(false)}>
            Cancel
          </Button>
          <Button variant="primary" onClick={() => answer(true)}>
            Add
          </Button>
        </div>
      </Dialog>
    </div>
  )
}
