import { render } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { MorphTitle } from './MorphTitle'
import { TIDY_MORPH_MS } from './tidied'

describe('MorphTitle', () => {
  it('writes the title in letter by letter within the morph, read out whole', () => {
    const { container } = render(<MorphTitle text="Testing Domain" />)
    const title = container.querySelector('[data-morph]')
    expect(title?.getAttribute('aria-label')).toBe('Testing Domain')
    const letters = [...container.querySelectorAll<HTMLElement>('.clu-morph-letter')]
    expect(letters.map((l) => l.textContent).join('')).toBe('Testing Domain')
    const delays = letters.map((l) => parseInt(l.style.animationDelay, 10))
    expect(delays[0]).toBe(0)
    expect(delays.every((d, i) => i === 0 || d >= (delays[i - 1] ?? 0))).toBe(true)
    expect(Math.max(...delays) + 100).toBeLessThanOrEqual(TIDY_MORPH_MS)
    expect(container.querySelectorAll('.clu-morph-sparkle')).toHaveLength(3)
  })
  it('lands a very long title in time too', () => {
    const { container } = render(<MorphTitle text={'x'.repeat(500)} />)
    const delays = [...container.querySelectorAll<HTMLElement>('.clu-morph-letter')].map((l) => parseInt(l.style.animationDelay, 10))
    expect(Math.max(...delays) + 100).toBeLessThanOrEqual(TIDY_MORPH_MS)
  })
})
