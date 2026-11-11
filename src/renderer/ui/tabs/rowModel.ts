// What a tab row or favorite tile shows (02-tabs.md F2, F9, F11, design.md section 2). Pure, unit tested.
import type { TabState } from '../../../shared/state'
import { unreadCount } from '../../../shared/tabs'
import { displayHost } from '../../../shared/url'

export interface RowView {
  title: string
  /** Favorite or pinned: the close control shows a minus and keeps the row (F9.5). */
  anchored: boolean
  /** A loaded favorite or pinned row's control unloads it ("−"); the moment it is
   *  unloaded the same control removes the row ("×"). A Today row's always closes it ("×"). */
  close: 'unload' | 'remove' | 'close'
  /** A "/" sits between favicon and title, and the favicon returns to the pinned URL (F2). */
  offPinned: boolean
  /** F11: a speaker while audible, a crossed speaker while muted (even when silent), else nothing. */
  audio: 'playing' | 'muted' | null
  audioLabel: string
  /** The page's title carries an unread count ("(3) Feed", "Inbox (12) - Mail"): a favorite shows a red dot. */
  unread: boolean
}

/** An unread count in a page title, as mail and social sites put it; zero and junk count as none. */
export const hasUnread = (title: string): boolean => unreadCount(title) > 0

const bare = (u: string) => u.replace(/^[a-z][a-z0-9+.-]*:\/\//i, '').replace(/\/$/, '')

/** A page still loading has no title of its own yet: Chromium's stand-in is its URL. The row reads "Untitled"
 *  until the page names itself. */
function untitled(t: TabState): boolean {
  if (!t.loading || t.titleOverride) return false
  return !t.title || bare(t.title) === bare(t.url) || t.title === displayHost(t.url)
}

export function rowView(t: TabState): RowView {
  const anchored = t.section !== 'today'
  const audio = t.muted ? 'muted' : t.audible ? 'playing' : null
  return {
    title: untitled(t) ? 'Untitled' : t.titleOverride || t.title || displayHost(t.url) || t.url,
    anchored,
    close: !anchored ? 'close' : t.lifecycle === 'unloaded' || t.lifecycle === 'hibernated' ? 'remove' : 'unload',
    offPinned: anchored && t.offPinned,
    audio,
    audioLabel: t.muted ? 'Unmute this tab' : 'Mute this tab',
    unread: hasUnread(t.title)
  }
}
