import { describe, expect, it } from 'vitest'
import { noteSpaces } from './NewSpaceCard'

describe('noteSpaces', () => {
  it('marks the spaces of the first render as old, later ones with the time they joined, and drops gone ones', () => {
    const seen = new Map<string, number>()
    noteSpaces(seen, ['a', 'b'], 100, true)
    expect(seen.get('a')).toBe(-Infinity)
    noteSpaces(seen, ['a', 'c', 'b'], 500, false)
    expect(seen.get('c')).toBe(500)
    // A later patch keeps the first time it was seen.
    noteSpaces(seen, ['a', 'c', 'b'], 900, false)
    expect(seen.get('c')).toBe(500)
    noteSpaces(seen, ['a', 'c'], 1000, false)
    expect(seen.has('b')).toBe(false)
  })
})
