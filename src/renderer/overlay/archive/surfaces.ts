// Overlay surfaces of the archive module: the Ctrl+Tab switcher. Clear's Undo uses the core toast.
import type { SurfaceRegistry } from '../core/types'
import { TabSwitcher } from './TabSwitcher'
import './switcher.css'

export const surfaces: SurfaceRegistry = { 'tab-switcher': TabSwitcher }
