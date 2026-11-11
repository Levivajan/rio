// The spaces module's commands.
import { entry, range } from './define'

export const spacesCommands = [
  entry({ id: 'window.newPrivate', title: 'New Private Window', accelerator: 'Cmd+Shift+N', menu: 'File', route: 'router', palette: true }),
  ...range(9).map((n) => entry({ id: `spaces.goto${n}`, title: `Space ${n}`, accelerator: `Ctrl+${n}`, menu: 'Spaces', route: 'router' })),
  entry({ id: 'spaces.next', title: 'Next Space', accelerator: 'Cmd+Option+Right', menu: 'Spaces', route: 'menu', palette: true }),
  entry({ id: 'spaces.prev', title: 'Previous Space', accelerator: 'Cmd+Option+Left', menu: 'Spaces', route: 'menu', palette: true }),
  // The space editor; also the ids of the items spaces adds to the sidebar's empty-area and + menus (PLAN 8.2).
  entry({ id: 'spaces.newSpace', title: 'New Space…', menu: 'Spaces', route: 'menu', palette: true }),
  entry({ id: 'spaces.editSpace', title: 'Edit Space…', menu: 'Spaces', route: 'menu', palette: true }),
  entry({ id: 'profiles.newProfile', title: 'New Profile…', menu: 'Spaces', route: 'menu', palette: true })
]
