// Renderer test setup (seeded by the scaffold): Testing Library cleanup, jest-dom matchers, stub window.clu.
import '@testing-library/jest-dom/vitest'
import { cleanup } from '@testing-library/react'
import { afterEach, beforeEach } from 'vitest'
import { setStateMirrorForTest } from '../renderer/common/store'
import { stubClu } from './stubClu'

beforeEach(() => {
  window.clu = stubClu().clu
  setStateMirrorForTest(null)
})
afterEach(() => cleanup())
