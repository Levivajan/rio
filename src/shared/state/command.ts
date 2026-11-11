// State fields of the command module.
import type { CommandMode } from '../ipc/command'
import type { TabId } from '../types'

/** `engineName` is the profile's search engine as the browser names it (Chromium's TemplateURLService); without it the
 *  bar labels search rows with the engine registry's name. */
export interface CommandWindowFields { command: { sessionId: string; mode: CommandMode; targetTabId: TabId | null; initialText: string; engineName?: string } | null }
export const defaultCommandWindow = (): CommandWindowFields => ({ command: null })
