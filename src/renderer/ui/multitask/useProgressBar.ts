// The Mini toolbar's progress bar (decision 37): shown while the page loads, then filled and faded out.
import { useEffect, useState } from 'react'

/** How long a finished bar stays at 100% while it fades (well inside the 400 ms budget). */
export const PROGRESS_LINGER_MS = 200

export function useProgressBar(loading: boolean, progress: number): { visible: boolean; value: number; done: boolean } {
  const [lingering, setLingering] = useState(false)
  const [wasLoading, setWasLoading] = useState(loading)
  if (wasLoading !== loading) {
    setWasLoading(loading)
    setLingering(!loading)
  }
  useEffect(() => {
    if (!lingering) return
    const t = setTimeout(() => setLingering(false), PROGRESS_LINGER_MS)
    return () => clearTimeout(t)
  }, [lingering])
  const value = Number.isFinite(progress) ? Math.min(1, Math.max(0, progress)) : 0
  return { visible: loading || lingering, value: loading ? value : 1, done: !loading }
}
