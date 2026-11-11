// Set Icon on a folder or pinned row (02-tabs.md F3, PLAN tabs.setIcon): a modal with one field that takes
// exactly one emoji grapheme. Anything else ('ab', two emoji) shows an inline error; main checks it again.
import { useId, useState } from 'react'
import { ICON_ERROR, isOneEmoji } from '../../../shared/tabs'
import { Dialog } from '../../common/Dialog'
import { closeInstance, type SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button, Input } from '../../common/controls'

const check = (value: string) => (value.trim() && !isOneEmoji(value.trim()) ? ICON_ERROR : null)

export function IconPrompt({ instance }: SurfaceProps) {
  const p = (instance.props ?? {}) as { nodeId?: string; icon?: string | null }
  const [value, setValue] = useState(p.icon ?? '')
  const [error, setError] = useState<string | null>(null)
  const fieldId = useId()
  const errorId = useId()
  const hintId = useId()
  const cancel = () => closeInstance(instance.instanceId)
  const save = async () => {
    const icon = value.trim()
    if (!isOneEmoji(icon) || !p.nodeId) return setError(ICON_ERROR)
    const r = await clu().tabs.setIcon({ nodeId: p.nodeId, icon })
    if (r.ok) cancel()
    else setError(r.error.reason === 'not-one-emoji' ? ICON_ERROR : "This item can't have an icon")
  }
  return (
    <div className="clu-center" data-surface="tabs.setIcon" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="Set Icon" onCancel={cancel}>
        <form
          onSubmit={(e) => {
            e.preventDefault()
            void save()
          }}
        >
          <label htmlFor={fieldId} className="clu-prompt-label">
            Emoji
          </label>
          <Input
            id={fieldId}
            className="is-emoji"
            value={value}
            maxLength={32}
            spellCheck={false}
            autoComplete="off"
            aria-invalid={error !== null}
            aria-describedby={error ? errorId : hintId}
            onChange={(e) => {
              setValue(e.target.value)
              setError(check(e.target.value))
            }}
          />
          <p id={hintId} className="clu-prompt-hint">
            Press Control-Command-Space to pick one.
          </p>
          <p id={errorId} role="alert" className="clu-prompt-error">
            {error ?? ''}
          </p>
          <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8 }}>
            <Button onClick={cancel}>
              Cancel
            </Button>
            <Button type="submit" variant="primary">
              Save
            </Button>
          </div>
        </form>
      </Dialog>
    </div>
  )
}
