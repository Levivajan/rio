// State fields of the spaces module.
import type { ProfileId, SpaceColor, SpaceIcon, SpaceId } from '../types'

export interface SpaceInit { id: SpaceId; name: string; icon: SpaceIcon; color: SpaceColor; profileId: ProfileId; ephemeral: boolean }
export interface SpacesSpaceFields { id: SpaceId; name: string; icon: SpaceIcon; color: SpaceColor; profileId: ProfileId; ephemeral: boolean }
export const defaultSpacesSpace = (init: SpaceInit): SpacesSpaceFields => ({ ...init })

export interface ProfileInit { id: ProfileId; name: string; isDefault: boolean; ephemeral: boolean }
export interface SpacesProfileFields { id: ProfileId; name: string; isDefault: boolean; ephemeral: boolean }
export const defaultSpacesProfile = (init: ProfileInit): SpacesProfileFields => ({ ...init })

export interface SpacesWindowFields {
  activeSpaceId: SpaceId
  swipe: { fraction: number; targetSpaceId: SpaceId | null } | null
  /** The frame's trackpad phases: `phased` once the device reports real phases, `lifts` counts finger lifts. */
  wheel: { phased: boolean; lifts: number }
}
export const defaultSpacesWindow = (activeSpaceId: SpaceId): SpacesWindowFields => ({ activeSpaceId, swipe: null, wheel: { phased: false, lifts: 0 } })

export interface SpacesTopFields { spaceOrder: SpaceId[] }
export const defaultSpacesTop = (): SpacesTopFields => ({ spaceOrder: [] })
