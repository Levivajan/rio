// The split module's commands.
import { entry, range } from './define'

export const splitCommands = [
  entry({ id: 'split.add', title: 'Add Split View', accelerator: 'Ctrl+Shift+=', menu: 'View', route: 'menu', palette: true }),
  entry({ id: 'split.separate', title: 'Separate Pane', accelerator: 'Ctrl+Shift+-', menu: 'View', route: 'menu', palette: true }),
  // View ▸ Expand Current Split: the focused pane takes the room the other panes' minimum leaves; again evens them out.
  entry({ id: 'split.expand', title: 'Expand Current Split', menu: 'View', route: 'menu' }),
  ...range(4).map((n) => entry({ id: `split.focus${n}`, title: `Focus Pane ${n}`, accelerator: `Ctrl+Shift+${n}`, menu: 'View', route: 'menu' })),
  entry({ id: 'split.next', title: 'Next Pane', accelerator: 'Ctrl+Shift+]', menu: 'View', route: 'menu' }),
  entry({ id: 'split.prev', title: 'Previous Pane', accelerator: 'Ctrl+Shift+[', menu: 'View', route: 'menu' }),
  // Sidebar row menu items (PLAN 8.2); from the app menu they act on the focused tab.
  entry({ id: 'split.addToSplit', title: 'Add to Split View', menu: 'Tabs', route: 'menu' }),
  entry({ id: 'split.openInSplit', title: 'Open in Split', menu: 'Tabs', route: 'menu' })
]
