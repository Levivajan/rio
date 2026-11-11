// Extensions menu (docs/chromium/m7.md): every extension of the window's profile under the action
// row's menu button. A row runs the extension's action (its popup hangs from the menu button, a side panel opens next
// to the page card), the pin button puts the action in the row or takes it out. Chromium's own page manages the rest.
import { useEffect, useRef } from 'react'
import { invoke } from '../../common/invoke'
import { useCluState } from '../../common/useCluState'
import { clu } from '../../common/clu'
import { closeInstance, type SurfaceProps } from '../core/types'
import { popoverPosition } from './SiteControlsPopover'
import './extensionsMenu.css'

export const STORE_URL = 'https://chromewebstore.google.com/'

function PinIcon({ on }: { on: boolean }) {
  return (
    <svg width="14" height="14" viewBox="0 0 14 14" fill={on ? 'currentColor' : 'none'} stroke="currentColor" strokeWidth="1.2" strokeLinejoin="round" aria-hidden="true">
      <path d="M5 1.5h4l-.6 3.6 2.1 2.1v1.3H7.6V12L7 12.8 6.4 12V8.5H3.5V7.2l2.1-2.1Z" />
    </svg>
  )
}

export function ExtensionsMenu({ instance, windowId }: SurfaceProps) {
  const row = useCluState((s) => s.windows[windowId]?.actionRow ?? null)
  // Pins belong to the profile: a private window shows them but changes none.
  const isPrivate = useCluState((s) => s.windows[windowId]?.private ?? false)
  const ref = useRef<HTMLDivElement>(null)
  const anchor = instance.rect ?? { x: 0, y: 0, width: 0, height: 0 }
  const close = () => closeInstance(instance.instanceId)
  const gone = !row

  useEffect(() => {
    if (gone) closeInstance(instance.instanceId)
  }, [gone, instance.instanceId])

  useEffect(() => {
    ref.current?.focus()
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape') return
      e.preventDefault()
      closeInstance(instance.instanceId)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [instance.instanceId])

  if (!row) return null
  const pos = popoverPosition(anchor, { width: window.innerWidth, height: window.innerHeight })
  return (
    <div ref={ref} role="dialog" aria-label="Extensions" tabIndex={-1} className="clu-extensions-menu" style={pos} onPointerDown={(e) => e.stopPropagation()}>
      {row.actions.length === 0 ? <p className="clu-extensions-menu-empty">No extensions yet</p> : null}
      <ul className="clu-extensions-menu-list">
        {row.actions.map((a) => (
          <li key={a.id} className="clu-extensions-menu-item" data-extension={a.id}>
            <button
              type="button"
              className="clu-extensions-menu-open"
              disabled={!a.enabled}
              onClick={() => {
                close()
                void invoke(clu().extensions.runAction({ id: a.id, anchor }))
              }}
            >
              {a.icon ? <img src={a.icon} alt="" width={16} height={16} draggable={false} /> : <span className="clu-extensions-menu-letter" aria-hidden="true">{a.name.slice(0, 1).toUpperCase()}</span>}
              <span className="clu-extensions-menu-name">{a.name}</span>
              {a.badge ? <span className="clu-extensions-menu-badge">{a.badge}</span> : null}
            </button>
            {isPrivate ? null : (
              <button
                type="button"
                className="clu-extensions-menu-pin"
                aria-pressed={a.pinned}
                aria-label={a.pinned ? `Unpin ${a.name}` : `Pin ${a.name}`}
                data-pinned={a.pinned}
                onClick={() => void invoke(clu().extensions.setPinned({ id: a.id, pinned: !a.pinned }))}
              >
                <PinIcon on={a.pinned} />
              </button>
            )}
          </li>
        ))}
      </ul>
      <div className="clu-extensions-menu-footer">
        <button
          type="button"
          className="clu-extensions-menu-link"
          onClick={() => {
            close()
            void invoke(clu().commands.run({ id: 'extensions.open' }))
          }}
        >
          Manage extensions
        </button>
        <button
          type="button"
          className="clu-extensions-menu-link"
          onClick={() => {
            close()
            void invoke(clu().tabs.create({ url: STORE_URL }))
          }}
        >
          Get extensions
        </button>
      </div>
    </div>
  )
}
