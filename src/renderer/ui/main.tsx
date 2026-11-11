// UI bundle entry.
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { trackKeyboardFocus } from '../common/keyboardFocus'
import { drawTooltipsInOverlay } from '../common/Tooltip'
import { App } from './App'
import './styles.css'

// The page card covers this document: its tooltips draw in the overlay.
drawTooltipsInOverlay(true)
// Sidebar rows ring only for keyboard focus, never after a mouse click.
trackKeyboardFocus()
const root = document.getElementById('root')
if (root) {
  createRoot(root).render(
    <StrictMode>
      <App />
    </StrictMode>
  )
}
