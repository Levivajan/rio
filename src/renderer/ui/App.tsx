// UI root: main or mini mode; sends ui.ready and ui:interactive after the first
// non-empty snapshot commit.
import { useEffect, useRef } from 'react'
import { markAfterPaint } from '../common/perf'
import { useAppearance, useReducedMotion } from '../common/useReducedMotion'
import { useCluState, useHasSnapshot, useWindowId } from '../common/useCluState'
import { Frame } from './frame/Frame'
import { Sidebar } from './sidebar/Sidebar'
import { clu } from '../common/clu'
// #region clu:multitask (edit only inside this block)
import { MiniApp } from './multitask/MiniApp'
// #endregion clu:multitask
// #region clu:theme (edit only inside this block)
import { Onboarding } from './theme/onboarding/Onboarding'
import { useTintChrome } from './theme/ThemeBackground'
// #endregion clu:theme

const mode = new URLSearchParams(window.location.search).get('mode') === 'mini' ? 'mini' : 'main'

export function App() {
  const windowId = useWindowId()
  const hasSnapshot = useHasSnapshot()
  const hasWindow = useCluState((s) => (windowId ? s.windows[windowId] !== undefined : false))
  const isPrivate = useCluState((s) => (windowId ? (s.windows[windowId]?.private ?? false) : false))
  const announced = useRef(false)
  useReducedMotion()
  // #region clu:theme (edit only inside this block)
  // The frame's chrome follows the space's tint, not only the mode.
  useAppearance(useTintChrome())
  // #endregion clu:theme

  useEffect(() => {
    if (!hasSnapshot || !hasWindow || announced.current) return
    announced.current = true
    void clu().ui.ready()
    markAfterPaint('ui:interactive')
  }, [hasSnapshot, hasWindow])

  useEffect(() => {
    const onViolation = (e: SecurityPolicyViolationEvent) =>
      void clu().log.write({ level: 'error', scope: 'csp', msg: 'securitypolicyviolation', data: { directive: e.violatedDirective } })
    document.addEventListener('securitypolicyviolation', onViolation)
    return () => document.removeEventListener('securitypolicyviolation', onViolation)
  }, [])

  if (!hasSnapshot || !hasWindow) return null
  if (mode === 'mini') return <MiniApp />
  return (
    <>
      {/* #region clu:theme (edit only inside this block) */}
      <Onboarding />
      {/* #endregion clu:theme */}
      {/* #region clu:platform (edit only inside this block) */}
      {/* #endregion clu:platform */}
      <Frame>
        <Sidebar mode={isPrivate ? 'private' : 'main'} />
      </Frame>
    </>
  )
}
