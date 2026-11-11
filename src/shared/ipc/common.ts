// Payload schemas used by more than one lane's contract.
import { finite, id, int, json, point, rect, str, url, z } from '../schema'
import { SPACE_COLORS, SPACE_SYMBOLS } from '../types'

export const anchor = () => z.union([rect(), point()])
export const spaceColor = () => z.enum(SPACE_COLORS)
export const spaceIcon = () =>
  z.union([
    z.object({ kind: z.literal('emoji'), value: str(16) }).strict(),
    z.object({ kind: z.literal('letter') }).strict(),
    z.object({ kind: z.literal('symbol'), value: z.enum(SPACE_SYMBOLS) }).strict()
  ])
export const spaceTheme = () =>
  z
    .object({
      colors: z.array(z.object({ h: finite().pipe(z.number().min(0).max(360)), s: finite().pipe(z.number().min(0).max(1)) }).strict()).min(1).max(3),
      intensity: finite().pipe(z.number().min(0).max(1)),
      grain: finite().pipe(z.number().min(0).max(1))
    })
    .strict()
export const dropTarget = () =>
  z.union([
    z.object({ section: z.literal('favorite'), index: int(0, 10_000) }).strict(),
    z.object({ section: z.literal('pinned'), spaceId: id(), parentId: id().nullable(), index: int(0, 10_000) }).strict(),
    z.object({ section: z.literal('today'), spaceId: id(), index: int(0, 10_000) }).strict()
  ])
export const commandId = () => z.string().regex(/^[a-zA-Z][a-zA-Z0-9]*(\.[a-zA-Z][a-zA-Z0-9]*)+$/).max(64)
export const commandResult = () =>
  z.union([
    z.object({ kind: z.literal('navigate'), url: url() }).strict(),
    z.object({ kind: z.literal('search'), engineId: str(32), query: str(2048) }).strict(),
    z.object({ kind: z.literal('tab'), tabId: id(), spaceId: id(), pinned: z.boolean(), url: url(), title: str(1024) }).strict(),
    z.object({ kind: z.literal('history'), url: url(), title: str(1024) }).strict(),
    z.object({ kind: z.literal('action'), actionId: str(64), args: json().optional() }).strict(),
    z.object({ kind: z.literal('reload'), tabId: id() }).strict(),
    z.object({ kind: z.literal('command'), id: commandId() }).strict(),
    z.object({ kind: z.literal('provided'), providerId: str(64), token: str(256) }).strict()
  ])
export const optionalTab = () => z.object({ tabId: id().optional() }).strict()
