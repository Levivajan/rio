// The app settings window's document (chrome://clu-settings, the internal:settings surface in Rio's settings window).
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { useAppearance } from '../../common/useReducedMotion'
import { SettingsWindow } from './SettingsWindow'
import '../internal.css'
import './settings.css'

function Page() {
  useAppearance()
  return <SettingsWindow />
}

const root = document.getElementById('root')
if (root) createRoot(root).render(<StrictMode><Page /></StrictMode>)
