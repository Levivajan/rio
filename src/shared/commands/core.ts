// Core commands.
import { entry } from './define'

export const coreCommands = [
  entry({ id: 'window.new', title: 'New Window', accelerator: 'Cmd+N', menu: 'File', route: 'router', palette: true }),
  entry({ id: 'window.close', title: 'Close Window', accelerator: 'Cmd+Shift+W', menu: 'File', route: 'router', variant: 'both' }),
  // Window ▸ Stay On Top keeps this window above the others until chosen again.
  entry({ id: 'window.stayOnTop', title: 'Stay On Top', aliases: ['Float window', 'Always on top'], menu: 'Window', route: 'menu', palette: true, variant: 'both' })
]
