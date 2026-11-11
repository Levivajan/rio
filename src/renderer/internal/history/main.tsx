// History page entry: clu://history.
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { useAppearance } from '../../common/useReducedMotion'
import { HistoryPage } from './HistoryPage'
import '../internal.css'
import './history.css'

function Page() {
  useAppearance()
  return <HistoryPage />
}

const root = document.getElementById('root')
if (root) createRoot(root).render(<StrictMode><Page /></StrictMode>)
