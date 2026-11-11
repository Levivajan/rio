// Inline rename field of a folder or tab row (02-tabs.md F3): focused with the text selected; Enter and
// blur commit, Esc cancels. Keys and clicks stay inside the field, so the row neither toggles, activates nor drags.
import { useEffect, useRef } from 'react'
import { cleanName, MAX_FOLDER_NAME } from '../../../shared/tabs'

/** onDone gets the cleaned value on commit (possibly empty) or null on cancel. */
export function RenameField({ value, label, onDone }: { value: string; label: string; onDone(next: string | null): void }) {
  const ref = useRef<HTMLInputElement>(null)
  const done = useRef(false)
  useEffect(() => {
    ref.current?.focus()
    ref.current?.select()
  }, [])
  const finish = (next: string | null) => {
    if (done.current) return
    done.current = true
    onDone(next)
  }
  const stop = (e: { stopPropagation(): void }) => e.stopPropagation()
  return (
    <input
      ref={ref}
      className="clu-rename-field"
      aria-label={label}
      defaultValue={value}
      maxLength={MAX_FOLDER_NAME}
      spellCheck={false}
      autoComplete="off"
      onKeyDown={(e) => {
        e.stopPropagation()
        if (e.key === 'Enter') finish(cleanName(e.currentTarget.value))
        else if (e.key === 'Escape') {
          e.preventDefault()
          finish(null)
        }
      }}
      onBlur={(e) => finish(cleanName(e.currentTarget.value))}
      onClick={stop}
      onDoubleClick={stop}
      onPointerDown={stop}
      onContextMenu={stop}
    />
  )
}
