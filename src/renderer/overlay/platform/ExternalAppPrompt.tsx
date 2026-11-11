// External-app prompt (docs/plan/09-platform.md F3): "Open “Mail”?" under the page frame's top edge.
import { useState } from 'react'
import { Dialog } from '../../common/Dialog'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

interface Props { promptId?: string; app?: string; origin?: string; persist?: boolean }

const CARD_WIDTH = 360

export function ExternalAppPrompt({ instance }: SurfaceProps) {
  const p = (instance.props ?? {}) as Props
  const [always, setAlways] = useState(false)
  const app = String(p.app ?? '').slice(0, 64)
  const origin = String(p.origin ?? '').slice(0, 512)
  const promptId = String(p.promptId ?? '')
  const answer = (action: 'open' | 'cancel') => void clu().externalApp.respond({ promptId, action, always: action === 'open' && always })
  const rect = instance.rect
  const left = rect ? Math.max(8, rect.x + rect.width / 2 - CARD_WIDTH / 2) : 8
  const top = rect ? rect.y + 8 : 8
  return (
    <div style={{ position: 'fixed', left, top }} data-prompt-id={promptId} onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title={`Open “${app}”?`} onCancel={() => answer('cancel')}>
        <p style={{ margin: '0 0 12px', color: 'var(--ink-2)', overflowWrap: 'anywhere' }}>{origin} wants to open this link in another app.</p>
        {p.persist !== false && (
          <label style={{ display: 'flex', alignItems: 'flex-start', gap: 8, margin: '0 0 16px', color: 'var(--ink)', fontSize: 'var(--fs-2)', overflowWrap: 'anywhere' }}>
            <input type="checkbox" checked={always} onChange={(e) => setAlways(e.target.checked)} style={{ margin: '1px 0 0', accentColor: 'var(--accent)' }} />
            <span>Always allow {origin} to open links of this type</span>
          </label>
        )}
        <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8 }}>
          <Button onClick={() => answer('cancel')}>
            Cancel
          </Button>
          <Button variant="primary" onClick={() => answer('open')}>
            Open “{app}”
          </Button>
        </div>
      </Dialog>
    </div>
  )
}
