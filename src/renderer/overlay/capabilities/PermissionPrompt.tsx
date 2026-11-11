// Permission prompt (engine 2.9): a passive surface in the top-right column of the page frame,
// so the page stays clickable around it. Allow and Block are remembered for the origin; the close button denies
// this request only.
import { useState } from 'react'
import { originHost, promptText } from '../../../shared/capabilities'
import { CloseIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import type { SurfaceProps } from '../core/types'
import './permissions.css'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

type Action = 'allow' | 'block' | 'dismiss'
const icon = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const CameraIcon = () => <svg {...icon}><rect x="1.8" y="4.2" width="9" height="7.6" rx="1.6" /><path d="m10.8 7 3.4-2v6l-3.4-2" /></svg>
const MicIcon = () => <svg {...icon}><rect x="5.7" y="1.8" width="4.6" height="8" rx="2.3" /><path d="M3.5 7.8a4.5 4.5 0 0 0 9 0M8 12.3v2" /></svg>
const PinIcon = () => <svg {...icon}><path d="M8 14.2s4.5-4.2 4.5-7.7a4.5 4.5 0 0 0-9 0c0 3.5 4.5 7.7 4.5 7.7z" /><circle cx="8" cy="6.5" r="1.6" /></svg>
const BellIcon = () => <svg {...icon}><path d="M4 11.2V7a4 4 0 0 1 8 0v4.2l1.2 1.3H2.8zM6.6 14h2.8" /></svg>
const ClipboardIcon = () => <svg {...icon}><rect x="3.2" y="2.8" width="9.6" height="11.4" rx="1.4" /><path d="M6 2.8V2h4v.8M5.8 7h4.4M5.8 9.8h3" /></svg>
const ScreenIcon = () => <svg {...icon}><rect x="1.8" y="2.8" width="12.4" height="8.4" rx="1.4" /><path d="M5.5 14h5M8 11.2V14" /></svg>

function ItemIcon({ items }: { items: string[] }) {
  if (items.includes('camera')) return <CameraIcon />
  if (items.includes('microphone')) return <MicIcon />
  if (items.includes('location')) return <PinIcon />
  if (items.includes('notifications')) return <BellIcon />
  if (items.includes('clipboard')) return <ClipboardIcon />
  return <ScreenIcon />
}

export function PermissionPrompt({ instance }: SurfaceProps) {
  const props = (instance.props ?? {}) as { promptId?: string; origin?: string; items?: string[] }
  const promptId = typeof props.promptId === 'string' ? props.promptId : ''
  const items = Array.isArray(props.items) ? props.items.filter((i): i is string => typeof i === 'string') : []
  const text = promptText(originHost(typeof props.origin === 'string' ? props.origin : ''), items)
  const [sent, setSent] = useState<string | null>(null)
  const respond = (action: Action) => {
    if (sent === promptId) return
    setSent(promptId)
    void clu().permission.respond({ promptId, action })
  }
  return (
    <div className="clu-permission-prompt" role="dialog" aria-labelledby={`prompt-${promptId}`} data-prompt-id={promptId} data-items={items.join(' ')}>
      <span className="clu-permission-prompt-icon"><ItemIcon items={items} /></span>
      <p id={`prompt-${promptId}`} className="clu-permission-prompt-text">{text}</p>
      <IconButton aria-label="Close" className="clu-permission-prompt-close" onClick={() => respond('dismiss')}>
        <CloseIcon />
      </IconButton>
      <div className="clu-permission-prompt-actions">
        <Button onClick={() => respond('block')}>Block</Button>
        <Button variant="primary" onClick={() => respond('allow')}>Allow</Button>
      </div>
    </div>
  )
}
