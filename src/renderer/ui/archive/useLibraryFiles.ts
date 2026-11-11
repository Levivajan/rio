// The files of one Library folder, read when the section opens, when a download finishes and when the
// window comes back to the front (a screenshot taken meanwhile shows up). A folder the user did not let Rio read lists
// nothing.
import { useEffect, useState } from 'react'
import type { LibraryFile, LibrarySource } from '../../../shared/ipc/capabilities'
import { clu } from '../../common/clu'
import { invoke } from '../../common/invoke'
import { cleanFiles } from './libraryModel'

/** `enabled` false (the hover peek while closed) reads nothing and keeps the last list. */
export function useLibraryFiles(source: LibrarySource, refreshKey: unknown, enabled = true): LibraryFile[] {
  const [files, setFiles] = useState<LibraryFile[]>([])
  const [focusTick, setFocusTick] = useState(0)
  useEffect(() => {
    const onFocus = () => setFocusTick((n) => n + 1)
    window.addEventListener('focus', onFocus)
    return () => window.removeEventListener('focus', onFocus)
  }, [])
  useEffect(() => {
    if (!enabled) return
    let live = true
    void invoke(clu().download.files({ source })).then((r) => {
      if (live) setFiles(r.ok ? cleanFiles(r.value) : [])
    })
    return () => {
      live = false
    }
  }, [source, refreshKey, focusTick, enabled])
  return files
}
