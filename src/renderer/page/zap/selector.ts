// Zap selectors (docs/plan/08-extensions.md "Selector generation"). picker.ts injects `zapTarget` into
// the page as source text, so it must stay self-contained: every helper lives inside it and it uses only the DOM.

export interface ZapTarget {
  /** Matches exactly the picked element. */
  selector: string
  /** Same tag and stable classes, no position; the exact selector when the element has no stable class. */
  related: string
  /** Tag plus up to 40 characters of its text. */
  label: string
}

export function zapTarget(el: Element): ZapTarget | null {
  const doc = el.ownerDocument
  if (!doc.body || el === doc.body || el === doc.documentElement || el.getRootNode() !== doc || !doc.body.contains(el)) return null
  // Plain CSS identifiers only, so nothing needs escaping and nothing can leave the rule.
  const ident = /^-?[A-Za-z_][A-Za-z0-9_-]*$/
  // Long names are generated, and the whole selector has to fit main's 1 KiB cap.
  const stable = (t: string): boolean => t.length <= 64 && ident.test(t) && !/\d{4,}/.test(t) && !/^(css|sc)-/i.test(t) && !/^_[a-z0-9]{5,}/i.test(t) && !/__[a-z0-9]{5,}$/i.test(t)
  const tagOf = (e: Element): string | null => (/^[a-z][a-z0-9-]*$/.test(e.localName) ? e.localName : null)
  const classesOf = (e: Element): string[] => (e.getAttribute('class') ?? '').split(/\s+/).filter(stable).slice(0, 3)
  const count = (sel: string): Element[] => {
    try {
      return Array.from(doc.querySelectorAll(sel))
    } catch {
      return []
    }
  }
  // getAttribute, because a form's `id` property can be shadowed by a named control.
  const idOf = (e: Element): string | null => {
    const id = e.getAttribute('id')
    return id && stable(id) && count(`#${id}`).length === 1 ? `#${id}` : null
  }
  const exactly = (sel: string): boolean => {
    const m = count(sel)
    return m.length === 1 && m[0] === el
  }
  const part = (e: Element): string => {
    const tag = tagOf(e)
    const same = Array.from(e.parentElement?.children ?? []).filter((s) => (tag ? s.localName === e.localName : true))
    const n = same.indexOf(e) + 1
    return `${tag ?? '*'}${classesOf(e).map((c) => `.${c}`).join('')}${tag ? `:nth-of-type(${n})` : `:nth-child(${n})`}`
  }

  const tag = tagOf(el) ?? '*'
  const classes = classesOf(el)
  const text = (el.textContent ?? '').replace(/\s+/g, ' ').trim().slice(0, 40).trim()
  const label = (text ? `${tag} · ${text}` : tag).slice(0, 80)

  let selector = idOf(el)
  if (!selector) {
    const parts: string[] = []
    let cur: Element | null = el
    for (let depth = 0; cur && depth < 8; depth++) {
      if (cur === doc.body) {
        parts.unshift('body')
        break
      }
      const id = cur === el ? null : idOf(cur)
      if (id) {
        parts.unshift(id)
        break
      }
      parts.unshift(part(cur))
      if (exactly(parts.join(' > '))) break
      cur = cur.parentElement
    }
    selector = parts.join(' > ')
  }
  if (selector.length > 1024 || !exactly(selector)) return null
  const byClass = classes.length > 0 ? `${tag}${classes.map((c) => `.${c}`).join('')}` : null
  const related = byClass && byClass.length <= 1024 && count(byClass).includes(el) ? byClass : selector
  return { selector, related, label }
}
