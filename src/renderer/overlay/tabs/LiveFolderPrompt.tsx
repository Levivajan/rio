// New Live Folder (the space menu's Live Folders ▸): one field for the feed a live folder follows, a feed
// address or a public GitHub repository. The browser checks the source again in folders.createLive; one that names no
// feed keeps the dialog open with an inline error. A feed that cannot be read later toasts from the browser.
import { useId, useState } from 'react'
import { Dialog } from '../../common/Dialog'
import { closeInstance, type SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button, Input } from '../../common/controls'

type Kind = 'feed' | 'githubReleases' | 'githubCommits'

const COPY: Record<Kind, { label: string; placeholder: string; error: string }> = {
  feed: { label: 'RSS or Atom feed address', placeholder: 'example.com/feed.xml', error: 'Enter a feed address, like example.com/feed.xml' },
  githubReleases: { label: 'Public GitHub repository', placeholder: 'owner/repository', error: 'Enter a public repository, like owner/repository' },
  githubCommits: { label: 'Public GitHub repository', placeholder: 'owner/repository', error: 'Enter a public repository, like owner/repository' }
}

const isKind = (v: unknown): v is Kind => v === 'feed' || v === 'githubReleases' || v === 'githubCommits'

/** The shape the browser accepts (LiveFolderFeedUrl): a web address with a dotted host, or "owner/repo" (a github.com
 *  URL too). Junk never reaches the browser; it checks again. */
export function looksLikeSource(kind: Kind, text: string): boolean {
  const t = text.trim()
  if (!t || /\s/.test(t)) return false
  if (kind !== 'feed') return /^(?:(?:https?:\/\/)?(?:www\.)?github\.com\/)?[\w.-]+\/[\w.-]+/i.test(t)
  try {
    const url = new URL(/^[a-z][a-z0-9+.-]*:\/\//i.test(t) ? t : `https://${t}`)
    return (url.protocol === 'https:' || url.protocol === 'http:') && url.hostname.includes('.') && !url.username && !url.password
  } catch {
    return false
  }
}

export function LiveFolderPrompt({ instance }: SurfaceProps) {
  const p = (instance.props ?? {}) as { spaceId?: unknown; kind?: unknown }
  const kind: Kind = isKind(p.kind) ? p.kind : 'feed'
  const spaceId = typeof p.spaceId === 'string' ? p.spaceId : ''
  const copy = COPY[kind]
  const [value, setValue] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const fieldId = useId()
  const errorId = useId()
  const cancel = () => closeInstance(instance.instanceId)
  const save = async () => {
    const source = value.trim()
    if (!spaceId || !looksLikeSource(kind, source)) return setError(copy.error)
    setBusy(true)
    const r = await clu().folders.createLive({ spaceId, kind, source: source.slice(0, 2048) })
    setBusy(false)
    if (r.ok) cancel()
    else setError(copy.error)
  }
  return (
    <div className="clu-center" data-surface="folders.live" data-kind={kind} onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="New Live Folder" onCancel={cancel}>
        <form
          onSubmit={(e) => {
            e.preventDefault()
            void save()
          }}
        >
          <label htmlFor={fieldId} className="clu-prompt-label">
            {copy.label}
          </label>
          <Input
            id={fieldId}
            value={value}
            placeholder={copy.placeholder}
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
            <Button onClick={cancel}>Cancel</Button>
            <Button type="submit" variant="primary" disabled={busy}>
              Create
            </Button>
          </div>
        </form>
      </Dialog>
    </div>
  )
}
