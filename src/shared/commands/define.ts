// Command table entry type.
export type MenuName = 'File' | 'Edit' | 'View' | 'History' | 'Tabs' | 'Spaces' | 'Window' | 'Help'
export type Route = 'router' | 'menu'
export type Variant = 'main' | 'mini' | 'both'

export interface CommandEntry {
  id: string
  title: string
  aliases?: string[]
  /** Chord like `Cmd+Shift+]`; matched by physical key code with exact modifiers. */
  accelerator?: string
  alt?: { keys: string; route?: Route }[]
  menu: MenuName
  variant: Variant
  route: Route
  palette: boolean
}

export const entry = (e: Omit<CommandEntry, 'variant' | 'palette'> & Partial<Pick<CommandEntry, 'variant' | 'palette'>>): CommandEntry => ({
  variant: 'main',
  palette: false,
  ...e
})

export const range = (n: number) => Array.from({ length: n }, (_, i) => i + 1)
