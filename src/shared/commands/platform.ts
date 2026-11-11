// The platform module's commands. Menu roles (quit, edit, window) are not commands.
import { entry } from './define'

export const platformCommands = [
  entry({ id: 'platform.openFile', title: 'Open File…', accelerator: 'Cmd+O', menu: 'File', route: 'menu', palette: true }),
  entry({ id: 'platform.shortcuts', title: 'Essential Keyboard Shortcuts', aliases: ['Shortcuts', 'Keyboard shortcuts'], menu: 'Help', route: 'menu', palette: true }),
  // Help ▸ Contact the Team opens CONTACT_URL (src/shared/command/rank.ts), as the command bar's help row does.
  entry({ id: 'platform.contactTeam', title: 'Contact the Team', menu: 'Help', route: 'menu' }),
  // Help ▸ Troubleshooting on the Mac (clu_main_menu.mm), with Open Task Manager.
  entry({ id: 'platform.showLogs', title: 'Reveal Rio Data', aliases: ['Show Logs in Finder'], menu: 'Help', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'platform.copyDiagnostics', title: 'Copy Rio Info', aliases: ['Copy Diagnostics'], menu: 'Help', route: 'menu', palette: true, variant: 'both' }),
  entry({ id: 'platform.aboutPage', title: 'About Rio Page', menu: 'Help', route: 'menu', palette: true, variant: 'both' }),
  // The Mac main menu shows it in the app menu, after Settings… (clu_main_menu.mm); macOS asks the user.
  entry({ id: 'platform.setDefaultBrowser', title: 'Set as Default Browser', menu: 'Help', route: 'menu' }),
  // The Mac main menu shows it in the app menu, after About Rio (clu_main_menu.mm).
  entry({ id: 'platform.checkForUpdates', title: 'Check for Updates…', aliases: ['update'], menu: 'Help', route: 'menu', palette: true })
]
