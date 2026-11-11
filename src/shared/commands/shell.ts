// The shell module's commands.
import { entry } from './define'

export const shellCommands = [
  entry({ id: 'sidebar.toggle', title: 'Toggle Sidebar', accelerator: 'Cmd+S', alt: [{ keys: 'Ctrl+Cmd+S', route: 'router' }], menu: 'View', route: 'menu', palette: true }),
  entry({ id: 'shell.focusMode', title: 'Focus Mode', accelerator: 'Cmd+Shift+F', menu: 'View', route: 'menu', palette: true }),
  // View > Show Toolbar: the URL bar across the top of the page; the same key turns it off.
  entry({ id: 'shell.toolbar', title: 'Show Toolbar', accelerator: 'Cmd+Shift+D', menu: 'View', route: 'menu', palette: true })
]
