// Toast: one passive surface at the window's top right, role=status; the browser keeps one toast at a time.
import { useEffect, useState } from 'react'
import type { ToastState } from '../../../shared/state'
import { useCluState } from '../../common/useCluState'
import type { SurfaceProps } from './types'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'
import { IconButton } from '../../common/IconButton'

const NONE: never[] = []
// The copy toast's action: a share button, which opens macOS's share picker at the toast.
export const SHARE_ACTION = 'siteControls.share'
/** The exit motion: the entry's slide and fade reversed, ending as the browser removes the toast at `until`. */
export const TOAST_EXIT_MS = 180

export function ToastSurface({ windowId }: SurfaceProps) {
  const toasts = useCluState((s) => s.windows[windowId]?.toasts ?? NONE)
  return (
    <div role="status" aria-live="polite" className="clu-toasts">
      {toasts.map((t) => (
        <Toast key={t.id} toast={t} />
      ))}
    </div>
  )
}

function Toast({ toast: t }: { toast: ToastState }) {
  const [leaving, setLeaving] = useState(() => t.until - Date.now() <= TOAST_EXIT_MS)
  useEffect(() => {
    const wait = t.until - Date.now() - TOAST_EXIT_MS
    if (wait <= 0) return setLeaving(true)
    const timer = setTimeout(() => setLeaving(true), wait)
    return () => clearTimeout(timer)
  }, [t.until])
  return (
    <div className="clu-toast" data-toast-id={t.id} data-leaving={leaving || undefined}>
      <span>{t.text}</span>
      {t.action?.commandId === SHARE_ACTION ? (
        <IconButton aria-label={t.action.label} className="clu-toast-share" data-toast-share onClick={() => void clu().siteControls.share({})}>
          <svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
            <path d="M8 1.75v8M5.25 4.5 8 1.75l2.75 2.75M5.5 6.75h-1.25v7.5h7.5v-7.5H10.5" />
          </svg>
        </IconButton>
      ) : t.action ? (
        <Button onClick={() => void clu().commands.run({ id: t.action?.commandId ?? '' })}>{t.action.label}</Button>
      ) : null}
    </div>
  )
}
