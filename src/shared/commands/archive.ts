// The archive module's commands.
import { entry } from './define'

export const archiveCommands = [
  entry({ id: 'archive.reopenClosed', title: 'Reopen Closed Tab', accelerator: 'Cmd+Shift+T', menu: 'File', route: 'router', palette: true }),
  entry({ id: 'archive.mruNext', title: 'Next Recent Tab', accelerator: 'Ctrl+Tab', menu: 'Tabs', route: 'router' }),
  entry({ id: 'archive.mruPrev', title: 'Previous Recent Tab', accelerator: 'Ctrl+Shift+Tab', menu: 'Tabs', route: 'router' }),
  entry({ id: 'archive.clearToday', title: 'Clear Today Tabs', accelerator: 'Cmd+Shift+K', menu: 'Tabs', route: 'menu', palette: true }),
  // The Undo button of Clear's toast runs this; it does nothing once the toast has ended.
  entry({ id: 'archive.undoClear', title: 'Undo Clear', menu: 'Tabs', route: 'menu' }),
  entry({ id: 'archive.open', title: 'Open Library…', accelerator: 'Cmd+Shift+L', menu: 'Window', route: 'menu', palette: true })
]
