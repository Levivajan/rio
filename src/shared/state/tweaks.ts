// State fields of the tweaks module.
export interface TweaksTabFields {
  tweak: { enabled: boolean; hasCss: boolean; hasJs: boolean; hasControls: boolean; zapCount: number } | null
  zapMode: { active: true; lastResult: string | null } | null
}
export const defaultTweaksTab = (): TweaksTabFields => ({ tweak: null, zapMode: null })
