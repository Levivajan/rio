// Basic auth dialog (06 2.12). What is typed goes straight to main in prompt.respond and into no state.
import { useState } from 'react'
import { Dialog } from '../../common/Dialog'
import type { SurfaceProps } from '../core/types'
import { clu } from '../../common/clu'
import { Button, Input } from '../../common/controls'

export function AuthDialog({ instance }: SurfaceProps) {
  const p = instance.props as { promptId: string; origin: string; realm: string; proxy: boolean; insecure: boolean; wrong: boolean }
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const cancel = () => void clu().prompt.respond({ promptId: p.promptId, action: 'cancel' })
  return (
    <div className="clu-center clu-prompt" onPointerDown={(e) => e.stopPropagation()}>
      <Dialog title="Sign in" onCancel={cancel}>
        <form
          onSubmit={(e) => {
            e.preventDefault()
            void clu().prompt.respond({ promptId: p.promptId, action: 'sign-in', values: { username, password } })
          }}
        >
          <p className="clu-prompt-text">{p.proxy ? `The proxy ${p.origin} asks for a username and password.` : `${p.origin} asks for a username and password.`}</p>
          {p.realm ? <p className="clu-prompt-text">It calls this area “{p.realm}”.</p> : null}
          {p.insecure ? <p className="clu-prompt-notice">Your connection to this site is not private.</p> : null}
          {p.wrong ? (
            <p className="clu-prompt-notice" role="alert">
              Wrong username or password
            </p>
          ) : null}
          <label className="clu-prompt-field">
            Username
            <Input type="text" autoComplete="off" spellCheck={false} value={username} onChange={(e) => setUsername(e.target.value)} />
          </label>
          <label className="clu-prompt-field">
            Password
            <Input type="password" autoComplete="off" value={password} onChange={(e) => setPassword(e.target.value)} />
          </label>
          <div className="clu-prompt-actions">
            <Button onClick={cancel}>
              Cancel
            </Button>
            <Button type="submit" variant="primary">
              Sign In
            </Button>
          </div>
        </form>
      </Dialog>
    </div>
  )
}
