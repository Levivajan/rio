// Extensions page entry.
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { ExtensionsPage } from './ExtensionsPage'

const root = document.getElementById('root')
if (root) createRoot(root).render(<StrictMode><ExtensionsPage /></StrictMode>)
