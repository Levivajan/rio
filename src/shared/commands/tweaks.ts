// Commands of the tweaks module (PLAN 8; docs/plan/09-platform.md Tabs menu, docs/plan/08-extensions.md command bar).
import { entry } from './define'

export const tweaksCommands = [
  entry({ id: 'tweaks.open', title: 'Site Tweaks…', aliases: ['Edit site tweaks', 'Tweak this site'], menu: 'Tabs', route: 'menu', palette: true }),
  entry({ id: 'tweaks.zap', title: 'Zap Elements…', aliases: ['Zap elements', 'Hide elements on this page'], menu: 'Tabs', route: 'menu', palette: true }),
  entry({ id: 'tweaks.toggle', title: 'Turn Site Tweaks On or Off', aliases: ['Turn off site tweaks for this site', 'Turn on site tweaks for this site'], menu: 'Tabs', route: 'menu', palette: true })
]
