// The multitask module's commands.
import { entry } from './define'

export const multitaskCommands = [
  entry({ id: 'peek.expand', title: 'Open Peek as Tab', accelerator: 'Cmd+Shift+O', menu: 'Tabs', route: 'menu' }),
  // Both variants: a Mini window opens another one too (and the chord is global outside test mode).
  entry({ id: 'mini.new', title: 'New Mini Window', accelerator: 'Cmd+Option+N', menu: 'File', route: 'router', variant: 'both', palette: true }),
  entry({ id: 'mini.openInSpace', title: 'Open in Space', accelerator: 'Cmd+O', menu: 'File', route: 'router', variant: 'mini' }),
  entry({ id: 'mini.pickSpace', title: 'Open in Space…', accelerator: 'Cmd+Shift+O', menu: 'File', route: 'router', variant: 'mini' })
]
