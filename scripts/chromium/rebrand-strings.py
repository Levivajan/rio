#!/usr/bin/env python3
"""Rewrites "Chromium" and the bare word "Chrome" to "Rio", the product name, in Chromium's product strings
(docs/chromium/PLAN.md 2.2, M1).

Covers every .grd whose messages name Chromium on Mac (GRDS, with their nested <part> files) and every .xtb
translation they list. Only <message> bodies change, so license headers and descriptions stay. Kept as is:
"ChromiumOS" / "Chromium OS" (not built on Mac) and "Chromium" as the open source project (the About line "made
possible by the Chromium open source project"). "The Chromium Authors" becomes "The Rio Authors", the company in BRANDING.
"Chrome" as a whole word becomes "Rio" too ("Sign in to Chrome", "Chrome's strongest security"), except in the other
products' names of brand_scan.CHROME_KEPT (Chrome Web Store, Google Chrome and the rest). The translations of a message
whose English keeps such a name are not rewritten for Chrome, since a translation may word that name differently.

grit identifies a translation by a fingerprint of the English text, so a rewritten message gets a new id. The script
parses each .grd with grit before and after the rewrite and moves every affected translation to its new id, which
keeps all locales translated. Some translators wrote "Chromium" or "Google Chrome" where the English has a
placeholder or plain "Chrome", so every translation whose English names neither is rebranded too. grit is imported
from the checkout (tools/grit) without writing bytecode there.

Prints each changed file relative to --src on stdout. --dry-run changes nothing in --src. --list prints every file
the rewrite reads.
"""

import argparse
import os
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
from brand_scan import BARE_CHROME, CHROME_KEPT  # pylint: disable=wrong-import-position

GRDS = [
    'chrome/app/chromium_strings.grd',
    'chrome/app/generated_resources.grd',
    'components/components_chromium_strings.grd',
    'components/components_strings.grd',
    # Omnibox action trigger words. The English lists say "google chrome" in lower case, and some translators wrote
    # "Google Chrome": those become "Rio", so the words stay in the locale packs without an upstream name.
    'components/omnibox/resources/omnibox_pedal_synonyms.grd',
    'components/privacy_sandbox_strings.grd',
    'extensions/strings/extensions_strings.grd',
]
MESSAGE = re.compile(r'(<message\b[^>]*>)(.*?)(</message>)', re.S)
TRANSLATION = re.compile(r'(<translation id=")(\d+)("[^>]*>)(.*?)(</translation>)', re.S)
PART = re.compile(r'<part file="([^"]+)"')
XTB = re.compile(r'<file path="([^"]+\.xtb)"')
AUTHORS = re.compile(r'The Chromium Authors')
UPSTREAM = re.compile(r'Chromium|Google Chrome')
PRODUCT = re.compile(r'Chromium(?!\s?OS)(?!<ph name="END_LINK_CHROMIUM")(?! open source project)')


def rebrand_chrome(text):
  """The bare word Chrome becomes Rio outside the kept names."""
  out, at = [], 0
  for m in CHROME_KEPT.finditer(text):
    out.append(BARE_CHROME.sub('Rio', text[at:m.start()]))
    out.append(m.group(0))
    at = m.end()
  out.append(BARE_CHROME.sub('Rio', text[at:]))
  return ''.join(out)


def rebrand(text):
  return rebrand_chrome(PRODUCT.sub('Rio', AUTHORS.sub('The Rio Authors', text)))


def keeps_names(english, translation):
  """True when the translation writes every kept Chrome name of its English the same way (an English variant like
  en-GB does), so rewriting its other Chrome words is safe."""
  need = {m.group(0).lower() for m in CHROME_KEPT.finditer(english)}
  return need <= {m.group(0).lower() for m in CHROME_KEPT.finditer(translation)}


def rebrand_translation(text, chrome=True):
  text = PRODUCT.sub('Rio', AUTHORS.sub('The Rio Authors', text)).replace('Google Chrome', 'Rio')
  return rebrand_chrome(text) if chrome else text


def rebrand_grd(text):
  return MESSAGE.sub(lambda m: m.group(1) + rebrand(m.group(2)) + m.group(3), text)


def message_ids(grit_dir, grd_path):
  """Returns [(name, id, English text)] for every <message> in document order, active or not."""
  sys.dont_write_bytecode = True
  if grit_dir not in sys.path:
    sys.path.insert(0, grit_dir)
  from grit import grd_reader  # pylint: disable=import-outside-toplevel
  from grit.node import message  # pylint: disable=import-outside-toplevel
  root = grd_reader.Parse(grd_path, debug=False, skip_validation_checks=True)
  msgs = [n.GetCliques()[0].GetMessage() for n in root.Preorder() if isinstance(n, message.MessageNode)]
  names = [n.attrs['name'] for n in root.Preorder() if isinstance(n, message.MessageNode)]
  return [(name, m.GetId(), m.GetPresentableContent()) for name, m in zip(names, msgs)]


def read(path):
  with open(path, encoding='utf-8') as f:
    return f.read()


def write(path, text):
  with open(path, 'w', encoding='utf-8', newline='') as f:
    f.write(text)


def grd_files(src, grd):
  """Returns the .grd with its nested <part> files, and the .xtb files it lists, relative to src."""
  sources, xtbs, todo = [], [], [grd]
  while todo:
    rel = todo.pop(0)
    sources.append(rel)
    text = read(os.path.join(src, rel))
    todo += [os.path.normpath(os.path.join(os.path.dirname(rel), p)) for p in PART.findall(text)]
    xtbs += [os.path.normpath(os.path.join(os.path.dirname(rel), p)) for p in XTB.findall(text)]
  return sources, xtbs


def process_grd(src, grit_dir, grd, dry_run, changed):
  sources, xtbs = grd_files(src, grd)
  before = message_ids(grit_dir, os.path.join(src, grd))

  # Rewrite into a scratch copy first: the new ids come from parsing it, and --dry-run never touches src.
  scratch = tempfile.mkdtemp(prefix='clu-strings-')
  try:
    rewritten = {}
    for rel in sources:
      text = read(os.path.join(src, rel))
      rewritten[rel] = rebrand_grd(text)
      os.makedirs(os.path.dirname(os.path.join(scratch, rel)), exist_ok=True)
      write(os.path.join(scratch, rel), rewritten[rel])
    after = message_ids(grit_dir, os.path.join(scratch, grd))
  finally:
    shutil.rmtree(scratch)

  if [b[0] for b in before] != [a[0] for a in after]:
    raise SystemExit(f'rebrand-strings: {grd}: message order changed after the rewrite')
  remap = {}
  for (name, old, _), (_, new, _) in zip(before, after):
    if old != new and remap.setdefault(old, new) != new:
      raise SystemExit(f'rebrand-strings: {grd}: {name}: id {old} maps to two new ids')
  # Messages that still name an upstream product on purpose (the open source project line): leave their translations.
  keep = {i for _, i, text in after if UPSTREAM.search(text)}
  # Messages whose English keeps another product's Chrome name: their translations keep Chrome, unless they write
  # those names as the English does.
  english = {i: text for _, i, text in after}
  kept_chrome = {i for i, text in english.items() if CHROME_KEPT.search(text)}

  def chrome_ok(msg_id, translation):
    return msg_id not in kept_chrome or keeps_names(english[msg_id], translation)

  for rel in sources:
    if rewritten[rel] != read(os.path.join(src, rel)):
      changed.append(rel)
      if not dry_run:
        write(os.path.join(src, rel), rewritten[rel])

  moved = 0
  for rel in xtbs:
    path = os.path.join(src, rel)
    if not os.path.exists(path):
      raise SystemExit(f'rebrand-strings: {grd} lists {rel}, which is missing')
    text = read(path)
    present = set(m.group(2) for m in TRANSLATION.finditer(text))
    # Two messages can read the same once rebranded: one translation per new id.
    emitted = set()

    def move(m):
      nonlocal moved
      old = m.group(2)
      if old not in remap:
        if old in keep:
          return m.group(0)
        return (m.group(1) + old + m.group(3) + rebrand_translation(m.group(4), chrome_ok(old, m.group(4))) +
                m.group(5))
      new = remap[old]
      if new in present or new in emitted:  # the rebranded text already has a translation; keep that one
        return ''
      emitted.add(new)
      moved += 1
      return m.group(1) + new + m.group(3) + rebrand_translation(m.group(4), chrome_ok(new, m.group(4))) + m.group(5)

    out = TRANSLATION.sub(move, text)
    if out != text:
      changed.append(rel)
      if not dry_run:
        write(path, out)
  return len(remap), moved, len(xtbs)


def main():
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument('--src', required=True, help='Chromium src root')
  parser.add_argument('--grit', help='grit directory (default: <src>/tools/grit)')
  parser.add_argument('--dry-run', action='store_true', help='report what would change, write nothing')
  parser.add_argument('--list', action='store_true', help='print every file the rewrite reads, change nothing')
  args = parser.parse_args()
  src = os.path.abspath(args.src)
  grit_dir = os.path.abspath(args.grit or os.path.join(src, 'tools', 'grit'))
  if not os.path.isfile(os.path.join(grit_dir, 'grit', 'grd_reader.py')):
    raise SystemExit(f'rebrand-strings: no grit at {grit_dir}')
  changed = []
  for grd in GRDS:
    if not os.path.isfile(os.path.join(src, grd)):
      raise SystemExit(f'rebrand-strings: {grd} is missing under {src}')
    if args.list:
      sources, xtbs = grd_files(src, grd)
      print('\n'.join(sources + xtbs))
      continue
    ids, moved, xtbs = process_grd(src, grit_dir, grd, args.dry_run, changed)
    print(f'rebrand-strings: {grd}: {ids} messages rebranded, {moved} translations moved across {xtbs} .xtb files',
          file=sys.stderr)
  for rel in changed:
    print(rel)


if __name__ == '__main__':
  main()
