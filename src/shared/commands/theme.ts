// Commands of the theme module. Edit Theme… opens the picker for the window's space (Spaces menu, the sidebar's
// empty-area menu through the tabs menu contributor, the command bar).
import { entry } from './define'

export const themeCommands = [
  entry({ id: 'theme.editTheme', title: 'Edit Theme…', aliases: ['Change space colors'], menu: 'Spaces', route: 'menu', palette: true }),
  // View ▸ Appearance ▸ Automatic, Light, Dark: the appearance.mode setting, checked on the one in use.
  entry({ id: 'theme.appearanceAuto', title: 'Automatic Appearance', menu: 'View', route: 'menu' }),
  entry({ id: 'theme.appearanceLight', title: 'Light Appearance', menu: 'View', route: 'menu' }),
  entry({ id: 'theme.appearanceDark', title: 'Dark Appearance', menu: 'View', route: 'menu' })
]
