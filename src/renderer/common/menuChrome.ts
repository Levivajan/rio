// The chrome set the calling WebUI wears (useAppearance writes it on the root), so a native menu opened from a light
// space draws light even when macOS is dark (favorite and space menus were dark gray on a light mint space).
export const menuLight = (): boolean => document.documentElement.dataset.theme === 'light'
