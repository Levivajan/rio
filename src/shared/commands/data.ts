// The data module's commands.
import { entry } from './define'

export const dataCommands = [
  entry({ id: 'history.open', title: 'Show History', accelerator: 'Cmd+Y', menu: 'History', route: 'menu', palette: true }),
  entry({ id: 'settings.open', title: 'Settings…', accelerator: 'Cmd+,', menu: 'File', route: 'menu', palette: true }),
  entry({ id: 'settings.browser', title: 'Browser Settings', aliases: ['Chromium settings', 'Site settings'], menu: 'File', route: 'menu', palette: true }),
  /** The import from another browser: the app settings at #import-chrome, the Chrome import open first. */
  entry({ id: 'settings.import', title: 'Import from Another Browser…', aliases: ['Switch browsers', 'Import passwords', 'Import bookmarks'], menu: 'File', route: 'menu', palette: true }),
  /** The Chrome import itself (Settings, General, #import-chrome): profile, what to bring, Import, the result. */
  entry({ id: 'settings.importChrome', title: 'Import from Google Chrome', aliases: ['Google Chrome import', 'Import bookmarks from Google Chrome'], menu: 'File', route: 'menu', palette: true }),
  /** The first run's switch for a Rio past its first run (Settings, General, its import card opened). */
  entry({ id: 'settings.importArc', title: 'Import from Arc', aliases: ['Bring everything over'], menu: 'File', route: 'menu', palette: true }),
  entry({ id: 'browsingData.open', title: 'Clear Browsing Data…', accelerator: 'Cmd+Shift+Backspace', menu: 'History', route: 'menu', palette: true })
]
