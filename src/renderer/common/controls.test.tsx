import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { Button, Card, Input, Row, Select, Toggle } from './controls'

describe('shared controls', () => {
  it('Button is a type="button" button with its variant, and a submit stays a submit', () => {
    render(
      <form>
        <Button>Cancel</Button>
        <Button variant="primary" type="submit">
          Save
        </Button>
      </form>
    )
    const cancel = screen.getByRole('button', { name: 'Cancel' })
    expect(cancel).toHaveAttribute('type', 'button')
    expect(cancel).toHaveAttribute('data-variant', 'default')
    expect(cancel).toHaveClass('clu-btn')
    expect(screen.getByRole('button', { name: 'Save' })).toHaveAttribute('type', 'submit')
    expect(screen.getByRole('button', { name: 'Save' })).toHaveAttribute('data-variant', 'primary')
  })

  it('Select is a native select inside the control, labelled by its row', () => {
    const onChange = vi.fn()
    render(
      <Row label="Search engine" htmlFor="engine" hint="Used by the command bar." hintId="engine-hint">
        <Select id="engine" value="google" aria-describedby="engine-hint" onChange={onChange}>
          <option value="google">Google</option>
          <option value="bing">Bing</option>
        </Select>
      </Row>
    )
    const select = screen.getByRole('combobox', { name: 'Search engine' })
    expect(select.parentElement).toHaveClass('clu-select')
    expect(select).toHaveAccessibleDescription('Used by the command bar.')
    fireEvent.change(select, { target: { value: 'bing' } })
    expect(onChange).toHaveBeenCalled()
  })

  it('Input is a text field unless told otherwise', () => {
    render(<Input aria-label="Address" defaultValue="https://example.com/" />)
    expect(screen.getByRole('textbox', { name: 'Address' })).toHaveAttribute('type', 'text')
    expect(screen.getByRole('textbox', { name: 'Address' })).toHaveClass('clu-input')
  })

  it('Toggle is a labelled switch that reports the next state', () => {
    const onChange = vi.fn()
    render(<Toggle label="Block ads and trackers" checked={false} onChange={onChange} />)
    const sw = screen.getByRole('switch', { name: 'Block ads and trackers' })
    expect(sw).toHaveAttribute('aria-checked', 'false')
    fireEvent.click(sw)
    expect(onChange).toHaveBeenCalledWith(true)
  })

  it('Card groups rows; a row without a control has no control column', () => {
    const { container } = render(
      <Card role="group" aria-label="About">
        <Row label="Version">
          <span>Rio 1.0</span>
        </Row>
        <Row label="No sites yet" />
      </Card>
    )
    expect(screen.getByRole('group', { name: 'About' })).toHaveClass('clu-group')
    expect(container.querySelectorAll('.clu-group-row')).toHaveLength(2)
    expect(container.querySelectorAll('.clu-group-row-control')).toHaveLength(1)
  })
})
