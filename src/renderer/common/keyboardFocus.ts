// Keyboard focus marks. Blink matches :focus-visible on a row that a mouse click focused as soon as another
// widget takes the keyboard (the folder popover, a menu: the frame is no longer focused and active), so a ring showed
// after a click. The document's root carries data-focus-keys while the last input was a key, and the sidebar's rings
// draw only then.
const MODIFIERS = new Set(['Meta', 'Control', 'Alt', 'Shift', 'CapsLock', 'Fn'])

export function trackKeyboardFocus(doc: Document = document): () => void {
  const root = doc.documentElement
  const key = (e: KeyboardEvent) => {
    // A shortcut chord moves no focus inside the sidebar.
    if (MODIFIERS.has(e.key) || e.metaKey || e.ctrlKey) return
    root.dataset.focusKeys = 'true'
  }
  const pointer = () => {
    delete root.dataset.focusKeys
  }
  doc.addEventListener('keydown', key, true)
  doc.addEventListener('pointerdown', pointer, true)
  return () => {
    doc.removeEventListener('keydown', key, true)
    doc.removeEventListener('pointerdown', pointer, true)
    pointer()
  }
}
