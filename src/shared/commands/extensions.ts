// Commands of the extensions module (docs/plan/08-extensions.md command bar). The blocking ones act on the active tab's site.
import { entry } from './define'

export const extensionsCommands = [
  entry({ id: 'blocking.turnOff', title: 'Turn Off Blocking on This Site', aliases: ['Turn off blocking on this site', 'Allow ads on this site'], menu: 'Tabs', route: 'menu', palette: true }),
  entry({ id: 'blocking.turnOn', title: 'Turn On Blocking on This Site', aliases: ['Turn on blocking on this site', 'Block ads on this site'], menu: 'Tabs', route: 'menu', palette: true }),
  entry({ id: 'extensions.open', title: 'Open Extensions', aliases: ['Open extensions', 'Manage extensions'], menu: 'Window', route: 'menu', palette: true })
]
