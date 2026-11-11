// About page entry: mounts AboutPage and the CSP violation reporter.
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { useAppearance } from '../../common/useReducedMotion'
import { reportCspViolations } from '../../ui/platform/cspReporter'
import { AboutPage } from './AboutPage'
import '../internal.css'
import './about.css'

reportCspViolations()

function Page() {
  useAppearance()
  return <AboutPage />
}

const root = document.getElementById('root')
if (root) createRoot(root).render(<StrictMode><Page /></StrictMode>)
