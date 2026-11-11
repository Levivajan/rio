// When a row or tile shows its speaker: about 0.8 s after a tab starts playing, so a
// click's blip or a short sound never flashes it; the crossed speaker of a muted tab and the end of playing show at once.
import { useEffect, useState } from 'react'
import { LOOK } from '../../../shared/look/tokens'
import type { RowView } from './rowModel'

export const SPEAKER_IN_MS = LOOK.sidebar.row.speakerInMs

export function useSpeakerSoon(audio: RowView['audio']): RowView['audio'] {
  const [shown, setShown] = useState<RowView['audio']>(audio === 'playing' ? null : audio)
  useEffect(() => {
    if (audio !== 'playing') {
      setShown(audio)
      return
    }
    const timer = setTimeout(() => setShown('playing'), SPEAKER_IN_MS)
    return () => clearTimeout(timer)
  }, [audio])
  return audio === 'playing' ? (shown === 'playing' ? 'playing' : null) : audio
}
