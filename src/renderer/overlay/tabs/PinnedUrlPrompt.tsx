// Change Pinned URL with a typed address (02-tabs.md F2): a modal with one field. Main checks the URL
// policy again in tabs.setPinnedUrl; a refused address keeps the dialog open with an inline error.
import { useId, useState } from 'react'
import { typedPinnedUrl } from '../../../shared/tabs'
import { Dialog } from '../../common/Dialog'
import { closeInstance, type SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button, Input } from '../../common/controls'

export function PinnedUrlPrompt({ instance }: SurfaceProps) {
  const p = (instance.props ?? {}) as { tabId?: string; url?: string }
  const [value, setValue] = useState(p.url ?? '')
  const [error, setError] = useState<string | null>(null)
  const fieldId = useId()
  const errorId = useId()
  const cancel = () => closeInstance(instance.instanceId)
  const save = async () => {
    const url = typedPinnedUrl(value)
    if (!url || !p.tabId) return setError('Enter a web address, like example.com')
    const r = await clu().tabs.setPinnedUrl({ tabId: p.tabId, url })
    if (r.ok) cancel()
    else setError("This address can't be pinned")
  }
  return (
    <div className="clu-center" data-surface="tabs.pinnedUrl" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="Change Pinned URL" onCancel={cancel}>
        <form
          onSubmit={(e) => {
            e.preventDefault()
            void save()
          }}
        >
          <label htmlFor={fieldId} className="clu-prompt-label">
            Address
          </label>
          <Input
            id={fieldId}
            value={value}
            maxLength={2048}
            spellCheck={false}
            autoComplete="off"
            aria-invalid={error !== null}
            {...(error ? { 'aria-describedby': errorId } : {})}
            onChange={(e) => {
              setValue(e.target.value)
              setError(null)
            }}
          />
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
