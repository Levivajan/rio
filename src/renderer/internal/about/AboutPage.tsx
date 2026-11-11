// clu://about (platform F4): versions, processes with memory, and the hibernated tabs.
import type { HibernationRow } from '../../../shared/ipc/capabilities'
import type { ProcessRow } from '../../../shared/ipc/platform'
import { useCluState } from '../../common/useCluState'
import { useAboutData, type Loaded } from './useAboutData'

const mb = (n: number) => `${Number.isFinite(n) ? n.toFixed(1) : '0.0'} MB`

function lastActive(at: number): string {
  if (!Number.isFinite(at) || at <= 0) return 'Never'
  const d = new Date(at)
  return Number.isNaN(d.getTime()) ? 'Never' : d.toLocaleString(undefined, { dateStyle: 'medium', timeStyle: 'short' })
}

function processLabel(p: ProcessRow): string {
  if (p.type === 'Tab') return p.titles.length ? p.titles.join(', ') : 'Renderer'
  if (p.type === 'Browser') return 'Rio'
  return p.name ?? p.type
}

function Status({ children }: { children: string }) {
  return <p className="about-status">{children}</p>
}

function Processes({ rows }: { rows: Loaded<ProcessRow> }) {
  if (rows === null) return <Status>Loading…</Status>
  if (rows === 'unavailable') return <Status>Not available</Status>
  const total = rows.reduce((sum, r) => sum + r.memoryMb, 0)
  return (
    <>
      <p className="about-status" data-total-mb={total.toFixed(1)}>
        {rows.length} {rows.length === 1 ? 'process' : 'processes'} using {mb(total)}
      </p>
      <table className="about-table" aria-labelledby="about-processes">
        <thead>
          <tr>
            <th scope="col">Process</th>
            <th scope="col">Type</th>
            <th scope="col" className="about-num">Memory</th>
            <th scope="col" className="about-num">PID</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((p) => (
            <tr key={p.pid} data-pid={p.pid} data-type={p.type}>
              <td data-col="label">{processLabel(p)}</td>
              <td data-col="type">{p.type}</td>
              <td data-col="memory" className="about-num">{mb(p.memoryMb)}</td>
              <td className="about-num">{p.pid}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </>
  )
}

function Hibernated({ rows }: { rows: Loaded<HibernationRow> }) {
  if (rows === null) return <Status>Loading…</Status>
  if (rows === 'unavailable') return <Status>Not available</Status>
  if (rows.length === 0) return <Status>No tabs are hibernated.</Status>
  return (
    <table className="about-table" aria-labelledby="about-hibernated">
      <thead>
        <tr>
          <th scope="col">Tab</th>
          <th scope="col">Reason</th>
          <th scope="col">Last active</th>
        </tr>
      </thead>
      <tbody>
        {rows.map((t) => (
          <tr key={t.tabId} data-tab-id={t.tabId}>
            <td data-col="title">{String(t.title || 'Untitled').slice(0, 200)}</td>
            <td data-col="reason">{t.reason ? String(t.reason).slice(0, 64) : 'None'}</td>
            <td>{lastActive(t.lastActiveAt)}</td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

export function AboutPage({ pollMs }: { pollMs?: number }) {
  const app = useCluState((s) => s.app)
  const { processes, hibernated } = useAboutData(pollMs)
  return (
    <main className="clu-internal about">
      <h1>About Rio</h1>
      <section aria-labelledby="about-versions">
        <h2 id="about-versions">Versions</h2>
        <dl>
          <dt>Rio</dt>
          <dd data-version="clu">{app.version}</dd>
          <dt>Chromium</dt>
          <dd data-version="chromium">{app.versions.chrome}</dd>
          <dt>Node</dt>
          <dd data-version="node">{app.versions.node}</dd>
        </dl>
      </section>
      <section aria-labelledby="about-processes" data-section="processes">
        <h2 id="about-processes">Processes</h2>
        <Processes rows={processes} />
      </section>
      <section aria-labelledby="about-hibernated" data-section="hibernated">
        <h2 id="about-hibernated">Hibernated tabs</h2>
        <Hibernated rows={hibernated} />
      </section>
    </main>
  )
}
