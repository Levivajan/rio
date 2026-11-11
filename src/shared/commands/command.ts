// The command module's commands.
import { entry } from './define'

export const commandCommands = [
  entry({ id: 'commandbar.newTab', title: 'New Tab', accelerator: 'Cmd+T', menu: 'File', route: 'router', variant: 'both' }),
  entry({ id: 'commandbar.editUrl', title: 'Open Location', accelerator: 'Cmd+L', menu: 'File', route: 'router', variant: 'both' }),
  entry({ id: 'edit.copyUrl', title: 'Copy URL', accelerator: 'Cmd+Shift+C', menu: 'Edit', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'edit.copyUrlMarkdown', title: 'Copy URL as Markdown', accelerator: 'Cmd+Shift+Option+C', menu: 'Edit', route: 'menu', palette: true, variant: 'both' })
]
