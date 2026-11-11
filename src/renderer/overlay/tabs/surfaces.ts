// Overlay surfaces of the tabs module.
import type { SurfaceRegistry } from '../core/types'
import { FolderPeek } from './FolderPeek'
import { IconPrompt } from './IconPrompt'
import { LiveFolderPrompt } from './LiveFolderPrompt'
import { PinnedUrlPrompt } from './PinnedUrlPrompt'
import { TabHoverCard } from './TabHoverCard'
import './tabs.css'

export const surfaces: SurfaceRegistry = { 'tabs.pinnedUrl': PinnedUrlPrompt, 'folders.live': LiveFolderPrompt, 'tabs.setIcon': IconPrompt, 'tab-hover': TabHoverCard, 'folder-peek': FolderPeek }
