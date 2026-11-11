// "Clu quit unexpectedly" notice (docs/plan/09-platform.md F14): a passive card after an unclean exit,
// with Show logs and Dismiss. Its own surface because the sentence and button need more room than a toast row.
import { RESTORED_NOTICE } from '../../../shared/platform/crash'
import { closeInstance, type SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function RestoredNotice({ instance }: SurfaceProps) {
  return (
    <div style={{ position: 'fixed', inset: 0, padding: 8, boxSizing: 'border-box' }}>
      <div
        role="status"
        aria-live="polite"
        data-surface="crash-restored"
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 12,
          height: '100%',
          boxSizing: 'border-box',
          padding: '0 12px 0 16px',
          borderRadius: 'var(--pop-r)',
          background: 'var(--surface)',
          color: 'var(--ink)',
          boxShadow: 'var(--shadow-2)',
          fontSize: 'var(--fs-row)',
          lineHeight: 1.4
        }}
      >
        <span style={{ flex: 1 }}>{RESTORED_NOTICE}</span>
        <Button onClick={() => void clu().commands.run({ id: 'platform.showLogs' })}>
          Show logs
        </Button>
        <Button onClick={() => closeInstance(instance.instanceId)}>
          Dismiss
        </Button>
      </div>
    </div>
  )
}
