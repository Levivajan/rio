// The capabilities module's commands.
import { entry } from './define'

export const capabilitiesCommands = [
  entry({ id: 'downloads.open', title: 'Downloads', accelerator: 'Cmd+Shift+J', menu: 'Window', route: 'menu', palette: true }),
  // Chromium's password manager page (docs/chromium/m9.md); no chord, as in Chrome.
  entry({ id: 'passwords.open', title: 'Passwords', aliases: ['Password manager', 'Saved passwords'], menu: 'Window', route: 'menu', palette: true }),
  entry({ id: 'permissions.openSite', title: 'Site Permissions…', menu: 'View', route: 'menu', palette: true })
]
