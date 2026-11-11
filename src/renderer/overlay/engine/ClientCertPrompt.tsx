// Client certificate picker (PLAN decision 10). Nothing starts selected, so no identity goes out without a click.
import { useState } from 'react'
import { Dialog } from '../../common/Dialog'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

export function ClientCertPrompt({ instance }: SurfaceProps) {
  const p = instance.props as { promptId: string; origin: string; items: string[] }
  const [selected, setSelected] = useState<number | null>(null)
  const cancel = () => void clu().prompt.respond({ promptId: p.promptId, action: 'cancel' })
  return (
    <div className="clu-center clu-prompt" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="Select a certificate" onCancel={cancel}>
        <p className="clu-prompt-text">{p.origin} asks you to identify yourself with a certificate.</p>
        <fieldset className="clu-prompt-list">
          <legend className="clu-prompt-text">Certificates</legend>
          {p.items.map((item, index) => (
            <label key={index} className="clu-prompt-option">
              <input type="radio" name={p.promptId} checked={selected === index} onChange={() => setSelected(index)} />
              {item}
            </label>
          ))}
        </fieldset>
        <div className="clu-prompt-actions">
          <Button onClick={cancel}>
            Cancel
          </Button>
          <Button
            variant="primary"
            disabled={selected === null}
            onClick={() => void clu().prompt.respond({ promptId: p.promptId, action: 'select', values: { index: String(selected) } })}
          >
            Continue
          </Button>
        </div>
      </Dialog>
    </div>
  )
}
