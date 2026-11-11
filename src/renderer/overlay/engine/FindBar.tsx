// Find bar (06 2.6): a passive surface at the top right of the page, so the page stays clickable.
import { useEffect, useRef, useState, type KeyboardEvent } from 'react'
import { IconButton } from '../../common/IconButton'
import { CloseIcon } from '../../common/icons'
import { useCluState } from '../../common/useCluState'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'

const MAX_TEXT = 1000

const Chevron = ({ up }: { up: boolean }) => (
  <svg width="14" height="14" viewBox="0 0 14 14" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    <path d={up ? 'M3.5 8.5 7 5l3.5 3.5' : 'M3.5 5.5 7 9l3.5-3.5'} />
  </svg>
)

export function FindBar({ instance, windowId }: SurfaceProps) {
  const { pageId, select } = instance.props as { pageId: string; select: number }
  const active = useCluState((s) => (s.tabs[pageId] ?? s.windows[windowId]?.peekPage)?.find?.active ?? 0)
  const total = useCluState((s) => (s.tabs[pageId] ?? s.windows[windowId]?.peekPage)?.find?.total ?? 0)
  const initial = useCluState((s) => (s.tabs[pageId] ?? s.windows[windowId]?.peekPage)?.find?.text ?? '')
  const [text, setText] = useState(initial)
  const input = useRef<HTMLInputElement>(null)

  useEffect(() => {
    input.current?.focus()
    input.current?.select()
  }, [select])

  const change = (value: string) => {
    setText(value)
    void clu().find.update({ tabId: pageId, text: value })
  }
  const step = (forward: boolean) => void clu().find.step({ tabId: pageId, forward })
  const close = () => void clu().find.close({ tabId: pageId })
  const onKeyDown = (e: KeyboardEvent) => {
    if (e.nativeEvent.isComposing) return
    if (e.key === 'Escape') {
      e.preventDefault()
      close()
    } else if (e.key === 'Enter') {
      e.preventDefault()
      step(!e.shiftKey)
    }
  }

  return (
    <div role="search" className="clu-engine-float clu-find-bar" onKeyDown={onKeyDown}>
      <input ref={input} aria-label="Find in page" placeholder="Find in page" maxLength={MAX_TEXT} spellCheck={false} value={text} onChange={(e) => change(e.target.value)} />
      <span role="status" className="clu-find-count">
        {text ? `${active} of ${total}` : ''}
      </span>
      <IconButton aria-label="Previous match" disabled={total === 0} onClick={() => step(false)}>
        <Chevron up />
      </IconButton>
      <IconButton aria-label="Next match" disabled={total === 0} onClick={() => step(true)}>
        <Chevron up={false} />
      </IconButton>
      <IconButton aria-label="Close find" onClick={close}>
        <CloseIcon />
      </IconButton>
    </div>
  )
}
