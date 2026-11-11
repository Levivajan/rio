// Back or forward arrow of a two-finger swipe (PLAN decision 45): passive, at the page's edge. It slides
// in with the fingers and fills once letting go would commit; main closes it after the settle.
import type { CSSProperties } from 'react'
import { NAV_SWIPE } from '../../../shared/engine'
import { BackIcon, ForwardIcon } from '../../common/icons'
import type { SurfaceProps } from '../core/types'

export function NavSwipeArrow({ instance }: SurfaceProps) {
  const { direction, fraction, settling } = instance.props as { direction: 'back' | 'forward'; fraction: number; settling: 'commit' | 'spring' | null }
  const progress = Math.min(1, Math.max(0, fraction) / NAV_SWIPE.commitFraction)
  return (
    <div className="clu-nav-arrow" data-direction={direction} data-armed={progress >= 1} data-settling={settling ?? undefined} style={{ '--progress': progress } as CSSProperties} aria-hidden="true">
      {direction === 'back' ? <BackIcon width={22} height={22} /> : <ForwardIcon width={22} height={22} />}
    </div>
  )
}
