import { render, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { stubClu } from '../../../test-utils/stubClu'
import { AboutPage } from './AboutPage'

const PROCESSES = [
  { pid: 10, type: 'Browser', name: null, memoryMb: 120.5, titles: [] },
  { pid: 12, type: 'Tab', name: null, memoryMb: 80, titles: ['Example', 'Docs'] },
  { pid: 13, type: 'Tab', name: null, memoryMb: 40, titles: [] },
  { pid: 11, type: 'GPU', name: 'GPU Process', memoryMb: 30.25, titles: [] }
]

describe('AboutPage', () => {
  it('lists processes with type, memory in MB and tab titles, and says Not available while hibernation.list is not implemented', async () => {
    window.clu = stubClu({ 'platform.processes': () => PROCESSES }).clu
    render(<AboutPage />)
    const table = await screen.findByRole('table', { name: 'Processes' })
    const rows = within(table).getAllByRole('row').slice(1)
    expect(rows.map((r) => within(r).getAllByRole('cell').map((c) => c.textContent))).toEqual([
      ['Rio', 'Browser', '120.5 MB', '10'],
      ['Example, Docs', 'Tab', '80.0 MB', '12'],
      ['Renderer', 'Tab', '40.0 MB', '13'],
      ['GPU Process', 'GPU', '30.3 MB', '11']
    ])
    expect(screen.getByText('4 processes using 270.8 MB')).toBeInTheDocument()
    const hibernated = screen.getByRole('region', { name: 'Hibernated tabs' })
    expect(within(hibernated).getByText('Not available')).toBeInTheDocument()
  })

  it('lists hibernated tabs with their reason and an empty state', async () => {
    const rows = [
      { tabId: 'a', title: 'Mail', lifecycle: 'hibernated', lastActiveAt: Date.UTC(2026, 10, 1, 9), reason: 'memory-pressure' },
      { tabId: 'b', title: '', lifecycle: 'hibernated', lastActiveAt: Number.NaN, reason: null }
    ]
    window.clu = stubClu({ 'hibernation.list': () => rows, 'platform.processes': () => [] }).clu
    render(<AboutPage />)
    const table = await screen.findByRole('table', { name: 'Hibernated tabs' })
    const cells = within(table).getAllByRole('row').slice(1).map((r) => within(r).getAllByRole('cell').map((c) => c.textContent))
    expect(cells[0]?.slice(0, 2)).toEqual(['Mail', 'memory-pressure'])
    expect(cells[1]).toEqual(['Untitled', 'None', 'Never'])
    expect(screen.getByText('0 processes using 0.0 MB')).toBeInTheDocument()

    window.clu = stubClu({ 'hibernation.list': () => [], 'platform.processes': () => 'junk' }).clu
    render(<AboutPage />)
    await waitFor(() => expect(screen.getByText('No tabs are hibernated.')).toBeInTheDocument())
    expect(within(screen.getAllByRole('region', { name: 'Processes' })[1] as HTMLElement).getByText('Not available')).toBeInTheDocument()
  })

  it('polls the process list', async () => {
    let n = 0
    window.clu = stubClu({ 'platform.processes': () => [{ pid: 1, type: 'Browser', name: null, memoryMb: ++n, titles: [] }] }).clu
    render(<AboutPage pollMs={20} />)
    await waitFor(() => expect(screen.getByText(/1 process using [3-9]\.0 MB/)).toBeInTheDocument())
  })
})
