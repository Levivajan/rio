// Internal page template: copied by each internal page. A React root over window.clu.
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { useAppearance } from '../../common/useReducedMotion'
import '../internal.css'

function Page() {
  useAppearance()
  return (
    <main className="clu-internal">
      <h1>Rio</h1>
    </main>
  )
}

const root = document.getElementById('root')
if (root) {
  createRoot(root).render(
    <StrictMode>
      <Page />
    </StrictMode>
  )
}
