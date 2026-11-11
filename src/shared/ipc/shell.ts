// Contract for the shell module.
import { empty, finite, point, rect, str, z } from '../schema'
import { defineCommand } from './define'

export const shellIpc = {
  'shell.toggleSidebar': defineCommand({ input: empty() }),
  'shell.toggleFocusMode': defineCommand({ input: empty() }),
  'shell.resetSidebarWidth': defineCommand({ input: empty() }),
  // `commit` marks the pointer-up call of a resize drag, which persists the width.
  'shell.setSidebarWidth': defineCommand({ input: z.object({ width: finite(), commit: z.boolean().optional() }).strict() }),
  'shell.setSidebarPeek': defineCommand({ input: z.object({ open: z.boolean() }).strict() }),
  'shell.pointerAlive': defineCommand({ input: empty(), allow: ['overlay'] }),
  // The floating sidebar holds itself open and takes the keyboard while the Library, or the bottom-left pop-up's row
  // menu or Rename, is open in it; false releases it.
  'shell.holdSidebarPeek': defineCommand({ input: z.object({ held: z.boolean() }).strict(), allow: ['overlay'] }),
  // Long press or right-click on back or forward: the nav-history menu under `anchor` (the button's rect).
  'shell.openNavHistory': defineCommand({ input: z.object({ direction: z.enum(['back', 'forward']), anchor: rect() }).strict() }),
  // Right-click on the URL pill or the toolbar's field: the native menu at `anchor` with Show as Toolbar or Hide
  // Toolbar.
  'shell.openToolbarMenu': defineCommand({ input: z.object({ anchor: point() }).strict() }),
  // The frame's tooltip pill, drawn in the overlay's passive layer so it sits above the page card: `rect`
  // is the pill plus its shadow margin in window coordinates as the frame laid it out, the shortcut's keycaps follow the
  // label, `above` turns its tight corner, `closing` plays the fade-out; no label hides it.
  'shell.tooltip': defineCommand({
    input: z
      .object({ label: str(200).optional(), keys: z.array(str(16)).max(8).optional(), rect: rect().optional(), above: z.boolean().optional(), closing: z.boolean().optional() })
      .strict(),
    allow: ['ui']
  })
} as const
