// Downloads popover (engine 2.10): light-dismiss, 360 px wide, scrolling rows above the sidebar
// footer. Rows show size and time left with Pause or Resume and Cancel; finished rows open on click and offer
// Show in Finder and Remove; failed rows offer Retry. The header opens the folder and clears finished rows.
import { useEffect, useRef, type SVGProps } from 'react'
import { downloadStatus, isActiveDownload, type DownloadState } from '../../../shared/capabilities'
import { CloseIcon, ReloadIcon } from '../../common/icons'
import { IconButton } from '../../common/IconButton'
import { useWindowDownloads } from '../../ui/capabilities/useWindowDownloads'
import { closeInstance, type SurfaceProps } from '../core/types'
import './downloads.css'
import { clu } from '../../common/clu'
import { Button } from '../../common/controls'

type Action = 'pause' | 'resume' | 'cancel' | 'retry' | 'open' | 'reveal' | 'remove'
const icon = { width: 16, height: 16, viewBox: '0 0 16 16', fill: 'none', stroke: 'currentColor', strokeWidth: 1.6, strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true } as const
const PauseIcon = (p: SVGProps<SVGSVGElement>) => <svg {...icon} {...p}><path d="M5.5 4v8M10.5 4v8" /></svg>
const ResumeIcon = (p: SVGProps<SVGSVGElement>) => <svg {...icon} {...p}><path d="M5.5 3.8v8.4L12 8z" /></svg>
const RevealIcon = (p: SVGProps<SVGSVGElement>) => <svg {...icon} {...p}><circle cx="7" cy="7" r="3.8" /><path d="m10 10 3 3" /></svg>
const FolderIcon = (p: SVGProps<SVGSVGElement>) => <svg {...icon} {...p}><path d="M2.5 4.5a1 1 0 0 1 1-1h3l1.5 1.5h4.5a1 1 0 0 1 1 1v5.5a1 1 0 0 1-1 1h-9a1 1 0 0 1-1-1z" /></svg>

const act = (id: string, action: Action) => void clu().download.act({ id, action })

function Row({ d }: { d: DownloadState }) {
  const status = downloadStatus(d)
  const fraction = d.totalBytes > 0 ? Math.min(1, d.receivedBytes / d.totalBytes) : null
  const canOpen = d.state === 'completed' && d.exists
  return (
    <li className="clu-download" data-download-id={d.id} data-state={d.state} data-exists={d.exists ? 'true' : 'false'}>
      <button type="button" className="clu-download-main" disabled={!canOpen} onClick={() => act(d.id, 'open')}>
        <span className="clu-download-name">{d.filename}</span>
        <span className="clu-download-status">{status}</span>
        {isActiveDownload(d.state) ? (
          <span className="clu-download-bar" data-indeterminate={fraction === null ? 'true' : 'false'} aria-hidden="true">
            <span style={{ width: `${Math.round((fraction ?? 0.3) * 100)}%` }} />
          </span>
        ) : null}
      </button>
      <span className="clu-download-actions">
        {d.state === 'progressing' ? <IconButton aria-label="Pause" onClick={() => act(d.id, 'pause')}><PauseIcon /></IconButton> : null}
        {d.state === 'paused' ? <IconButton aria-label="Resume" onClick={() => act(d.id, 'resume')}><ResumeIcon /></IconButton> : null}
        {d.state === 'interrupted' || d.state === 'cancelled' ? <IconButton aria-label="Retry" onClick={() => act(d.id, 'retry')}><ReloadIcon /></IconButton> : null}
        {isActiveDownload(d.state) || d.state === 'interrupted' ? <IconButton aria-label="Cancel" onClick={() => act(d.id, 'cancel')}><CloseIcon /></IconButton> : null}
        {d.state === 'completed' ? <IconButton aria-label="Show in Finder" disabled={!d.exists} onClick={() => act(d.id, 'reveal')}><RevealIcon /></IconButton> : null}
        {!isActiveDownload(d.state) && d.state !== 'interrupted' ? <IconButton aria-label="Remove" onClick={() => act(d.id, 'remove')}><CloseIcon /></IconButton> : null}
      </span>
    </li>
  )
}

export function DownloadsPopover({ instance, windowId }: SurfaceProps) {
  const props = (instance.props ?? {}) as { left?: number; bottom?: number }
  const downloads = useWindowDownloads(windowId)
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => ref.current?.focus(), [])
  const finished = downloads.some((d) => !isActiveDownload(d.state))
  return (
    <div
      ref={ref}
      className="clu-downloads"
      role="dialog"
      aria-label="Downloads"
      tabIndex={-1}
      style={{ left: props.left ?? 8, bottom: props.bottom ?? 44 }}
      onPointerDown={(e) => e.stopPropagation()}
      onKeyDown={(e) => {
        if (e.key === 'Escape') closeInstance(instance.instanceId)
      }}
    >
      <header className="clu-downloads-header">
        <h2>Downloads</h2>
        <IconButton aria-label="Open Downloads Folder" onClick={() => void clu().download.openFolder({})}>
          <FolderIcon />
        </IconButton>
        <Button disabled={!finished} onClick={() => void clu().download.clear({})}>
          Clear
        </Button>
      </header>
      {downloads.length === 0 ? (
        <p className="clu-downloads-empty">No downloads yet</p>
      ) : (
        <ul className="clu-downloads-list" aria-label="Downloads list">
          {downloads.map((d) => (
            <Row key={d.id} d={d} />
          ))}
        </ul>
      )}
    </div>
  )
}
