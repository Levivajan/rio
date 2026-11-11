// Modal dialog: role dialog, aria-modal, labelled title, focus trapped.
import { useEffect, useId, useRef, type ReactNode } from 'react'

export function Dialog({ title, children, onCancel }: { title: string; children: ReactNode; onCancel: () => void }) {
  const titleId = useId()
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const first = ref.current?.querySelector<HTMLElement>('button, [href], input, select, [tabindex]:not([tabindex="-1"])')
    first?.focus()
  }, [])
  return (
    <div
      ref={ref}
      role="dialog"
      aria-modal="true"
      aria-labelledby={titleId}
      className="clu-dialog"
      onKeyDown={(e) => {
        if (e.key === 'Escape') {
          e.preventDefault()
          onCancel()
          return
        }
        if (e.key !== 'Tab') return
        const nodes = [...(ref.current?.querySelectorAll<HTMLElement>('button, [href], input, select, [tabindex]:not([tabindex="-1"])') ?? [])]
        if (!nodes.length) return
        const first = nodes[0]
        const last = nodes[nodes.length - 1]
        if (e.shiftKey && document.activeElement === first) {
          e.preventDefault()
          last?.focus()
        } else if (!e.shiftKey && document.activeElement === last) {
          e.preventDefault()
          first?.focus()
        }
      }}
    >
      <h2 id={titleId} className="clu-dialog-title">
        {title}
      </h2>
      {children}
    </div>
  )
}
