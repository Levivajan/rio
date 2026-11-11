// The tabs module's commands.
import { entry, range } from './define'

export const tabsCommands = [
  entry({ id: 'tabs.close', title: 'Close Tab', accelerator: 'Cmd+W', menu: 'File', route: 'router', variant: 'both' }),
  ...range(9).map((n) => entry({ id: `tabs.goto${n}`, title: `Go to Tab ${n}`, accelerator: `Cmd+${n}`, menu: 'Tabs', route: 'router', variant: 'both' })),
  entry({ id: 'tabs.next', title: 'Next Tab', accelerator: 'Cmd+Option+Down', alt: [{ keys: 'Cmd+Shift+]' }], menu: 'Tabs', route: 'menu' }),
  entry({ id: 'tabs.prev', title: 'Previous Tab', accelerator: 'Cmd+Option+Up', alt: [{ keys: 'Cmd+Shift+[' }], menu: 'Tabs', route: 'menu' }),
  entry({ id: 'tabs.togglePin', title: 'Pin or Unpin Tab', accelerator: 'Cmd+D', menu: 'Tabs', route: 'menu', palette: true }),
  // No key: Cmd+Shift+D is Show Toolbar (shell.toolbar).
  entry({ id: 'tabs.duplicate', title: 'Duplicate Tab', menu: 'Tabs', route: 'menu', palette: true }),
  entry({ id: 'tabs.toggleMute', title: 'Mute or Unmute Tab', accelerator: 'Cmd+Shift+M', menu: 'Tabs', route: 'menu', palette: true }),
  // View ▸ Collapse Pinned Tabs, or Expand Pinned Tabs while the window's space has them collapsed.
  entry({ id: 'tabs.collapsePinned', title: 'Collapse Pinned Tabs', aliases: ['Expand Pinned Tabs'], menu: 'View', route: 'menu', palette: true }),
  // Tabs ▸ New Folder… (a pinned folder in the window's space, in rename), Reveal Tab in Sidebar (the sidebar shows the
  // active row, its folders open) and Reset all tabs in this Space (every pinned row back at its pinned URL).
  entry({ id: 'tabs.newFolder', title: 'New Folder…', menu: 'Tabs', route: 'menu' }),
  entry({ id: 'tabs.reveal', title: 'Reveal Tab in Sidebar', menu: 'Tabs', route: 'menu' }),
  entry({ id: 'tabs.resetAll', title: 'Reset all tabs in this Space', menu: 'Tabs', route: 'menu' })
]
