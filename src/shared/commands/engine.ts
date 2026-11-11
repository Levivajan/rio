// The engine module's commands.
import { entry } from './define'

export const engineCommands = [
  entry({ id: 'nav.back', title: 'Go Back', accelerator: 'Cmd+[', alt: [{ keys: 'Cmd+Left' }], menu: 'History', route: 'menu', variant: 'both' }),
  entry({ id: 'nav.forward', title: 'Go Forward', accelerator: 'Cmd+]', alt: [{ keys: 'Cmd+Right' }], menu: 'History', route: 'menu' }),
  entry({ id: 'nav.reload', title: 'Reload Page', accelerator: 'Cmd+R', menu: 'View', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'nav.hardReload', title: 'Hard Reload', accelerator: 'Cmd+Shift+R', menu: 'View', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'nav.stop', title: 'Stop Loading', accelerator: 'Cmd+.', menu: 'View', route: 'menu', variant: 'both' }),
  entry({ id: 'find.open', title: 'Find in Page', accelerator: 'Cmd+F', menu: 'Edit', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'find.next', title: 'Find Next', accelerator: 'Cmd+G', menu: 'Edit', route: 'menu', variant: 'both' }),
  entry({ id: 'find.prev', title: 'Find Previous', accelerator: 'Cmd+Shift+G', menu: 'Edit', route: 'menu', variant: 'both' }),
  entry({ id: 'zoom.in', title: 'Zoom In', accelerator: 'Cmd+=', alt: [{ keys: 'Cmd+Plus' }], menu: 'View', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'zoom.out', title: 'Zoom Out', accelerator: 'Cmd+-', menu: 'View', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'zoom.reset', title: 'Actual Size', accelerator: 'Cmd+0', menu: 'View', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'page.print', title: 'Print…', accelerator: 'Cmd+P', menu: 'File', route: 'menu', palette: true }),
  entry({ id: 'page.savePage', title: 'Save Page As…', accelerator: 'Cmd+Shift+S', menu: 'File', route: 'menu', palette: true }),
  // File ▸ Capture…: Chromium's region capture, as the site controls' Screenshot tile.
  entry({ id: 'page.capture', title: 'Capture…', aliases: ['Screenshot'], accelerator: 'Cmd+Shift+2', menu: 'File', route: 'menu', palette: true }),
  entry({ id: 'page.devtools', title: 'Developer Tools', accelerator: 'Cmd+Option+I', menu: 'View', route: 'menu', palette: true }),
  entry({ id: 'page.console', title: 'JavaScript Console', accelerator: 'Cmd+Option+J', menu: 'View', route: 'menu' }),
  entry({ id: 'popups.alwaysAllow', title: 'Always Allow Pop-ups from This Site', menu: 'View', route: 'menu' }),
  // View ▸ Clear Cookies and Refresh, Clear Cache and Refresh: the shown site's data only, then a reload.
  entry({ id: 'page.clearCookiesAndRefresh', title: 'Clear Cookies and Refresh', menu: 'View', route: 'menu', palette: true }),
  entry({ id: 'page.clearCacheAndRefresh', title: 'Clear Cache and Refresh', menu: 'View', route: 'menu', palette: true })
]
