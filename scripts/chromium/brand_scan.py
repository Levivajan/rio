#!/usr/bin/env python3
"""Scans a built Rio.app for user-visible upstream names (docs/chromium/PLAN.md 2.2, M1). Used by check-brand.sh.

Looks at every string in the locale .pak files (all of the UI text) and every string value in the app's and
framework's Info.plist and InfoPlist.strings files. Fails on "Chromium" and "Google Chrome" in any locale, on the bare
word "Chrome" (the product, where Rio is meant) in any locale, on the code name "Clu" (whole word, case-sensitive; the
product is Rio, docs/chromium/rio-rename.md) in any locale, on the forbidden words of forbidden_words() (whole word,
case-sensitive) in any locale, and on the banned names of docs/PLAN.md 1 in English (other
languages use some of them as ordinary words). The About line "made possible by the Chromium open source project",
the few phrases in ALLOWED and the other products in CHROME_KEPT pass (PLAN 2.2). URLs and code names (chrome://,
chrome.runtime, ChromeOS, Chromebook) are lower case or one word, so the whole-word, case-sensitive match skips them.
A string whose English names upstream or Chrome only inside an allowed phrase is allowed in every language, since
translations word it freely.

Usage: brand_scan.py APP [--pak FILE ...]   (--pak scans extra .pak files, for tests and partial builds)
Env: CLU_BRAND_WORDS (forbidden words, comma-separated), else ~/.clu/brand-words.txt (one per line, # comments).
"""

import argparse
import os
import plistlib
import re
import struct
import subprocess
import sys

UPSTREAM = re.compile(r'Chromium|Google Chrome')
CODE_NAME = re.compile(r'\bClu\b')
# Placeholder company names a fork must not ship. CLU_BRAND_WORDS or ~/.clu/brand-words.txt adds the builder's own
# (such as a company name kept out of the app); neither is part of the source tree.
DEFAULT_WORDS = ('Your Company', 'Example Inc')


def forbidden_words(env=None):
  """The default words plus CLU_BRAND_WORDS (comma-separated) or, when that is unset, ~/.clu/brand-words.txt."""
  env = os.environ if env is None else env
  extra = env.get('CLU_BRAND_WORDS')
  if extra is None:
    path = os.path.join(env.get('HOME', os.path.expanduser('~')), '.clu', 'brand-words.txt')
    try:
      with open(path, encoding='utf-8') as f:
        extra = ','.join(line.split('#', 1)[0] for line in f)
    except OSError:
      extra = ''
  words = list(DEFAULT_WORDS)
  for word in extra.split(','):
    word = word.strip()
    if word and word not in words:
      words.append(word)
  return words


COMPANY = re.compile(r'\b(?:' + '|'.join(re.escape(w) for w in forbidden_words()) + r')\b')
BANNED = re.compile(r'\b(Arc|Browser Company|Little Arc|Boosts?|Easel|Arc Max)\b')
# Allowed on purpose, each a true statement about someone else's product:
#   the open source project and its link in About; ChromiumOS (not built on Mac);
#   "Google Chrome help" (settings search, links to Google's Chrome help center); Family Link's "Google Chrome
#   settings" for a supervised child; "Boost productivity", the Chrome Web Store category, not the banned feature;
#   "Import from Arc", the browser an import reads from.
ALLOWED = re.compile(r'>Chromium</a>|Chromium open source project|\bChromium ?OS\b|Google Chrome help|'
                     r'Family Link\b.*Google Chrome settings|Boost productivity|Import from Arc')
# The bare word Chrome, which rebrand-strings.py rewrites to Rio outside CHROME_KEPT.
BARE_CHROME = re.compile(r'\bChrome\b')
# Other products' names that keep "Chrome", each Google's: the extension store, the operating system, the certificate
# root program, the enterprise products, Chrome Apps (the app platform), the developer docs, Google's AI features and
# Google Chrome itself (the browser a profile or bookmarks are imported from). rebrand-strings.py imports this list.
CHROME_KEPT = re.compile(r'Chrome Web Store|\bChrome OS\b|\bChrome Root (?:Store|Program)\b|\bChrome Enterprise\b|'
                         r'\bChrome Browser Cloud Management\b|\bChrome Remote Desktop\b|\bChrome for Testing\b|'
                         r'\bChrome [Aa]pps?\b|\bChrome Extensions? (?:and Chrome Web Store|development)|'
                         r'\bGemini in Chrome\b|\bGoogle Search in Chrome\b|\bGoogle Chrome\b')


def pak_resources(data):
  """Yields (resource id, bytes) from a version 4 or 5 .pak file."""
  version = struct.unpack_from('<I', data)[0]
  if version == 5:
    encoding, count = struct.unpack_from('<IH', data, 4)  # then the alias count; aliases repeat entries
    table = 12
  elif version == 4:
    count, encoding = struct.unpack_from('<IB', data, 4)
    table = 9
  else:
    raise ValueError(f'unknown .pak version {version}')
  entries = [struct.unpack_from('<HI', data, table + 6 * i) for i in range(count + 1)]
  if any(b[1] < a[1] for a, b in zip(entries, entries[1:])) or entries[-1][1] > len(data):
    raise ValueError('corrupt .pak index')
  for (rid, start), (_, end) in zip(entries, entries[1:]):
    yield rid, data[start:end], encoding


def pak_strings(path):
  with open(path, 'rb') as f:
    data = f.read()
  for rid, blob, encoding in pak_resources(data):
    if blob[:2] in (b'\x1f\x8b', b'\x1e\x9b'):  # gzip or brotli: not UI text
      continue
    try:
      yield rid, blob.decode('utf-16-le' if encoding == 2 else 'utf-8')
    except UnicodeDecodeError:
      continue


def plist_strings(path):
  """Yields every string value in a plist or .strings file."""
  try:
    with open(path, 'rb') as f:
      doc = plistlib.load(f)
  except Exception:  # pylint: disable=broad-except
    xml = subprocess.run(['plutil', '-convert', 'xml1', '-o', '-', path], capture_output=True, check=True).stdout
    doc = plistlib.loads(xml)
  stack = [doc]
  while stack:
    v = stack.pop()
    if isinstance(v, dict):
      stack.extend(v.values())
    elif isinstance(v, list):
      stack.extend(v)
    elif isinstance(v, str):
      yield v


def bare_chrome(text):
  return BARE_CHROME.search(CHROME_KEPT.sub('', ALLOWED.sub('', text))) is not None


def problems_in(text, english, allowed_upstream=False, allowed_chrome=False):
  found = []
  if not allowed_upstream and UPSTREAM.search(ALLOWED.sub('', text)):
    found.append('upstream name')
  if not allowed_chrome and bare_chrome(text):
    found.append('Chrome')
  if english and BANNED.search(ALLOWED.sub('', text)):
    found.append('banned name')
  if CODE_NAME.search(text):
    found.append('code name')
  if COMPANY.search(text):
    found.append('company name')
  return found


def scan(app, extra_paks):
  paks, plists = list(extra_paks), []
  for root, _, files in os.walk(app):
    for name in files:
      path = os.path.join(root, name)
      if name.endswith('.pak') and (root.endswith('.lproj') or os.path.basename(root) == 'locales'):
        paks.append(path)
      elif name in ('Info.plist', 'InfoPlist.strings') and '/Helpers/' not in path:
        plists.append(path)
  hits, strings = [], []
  for path in paks:
    lang = os.path.basename(os.path.dirname(path)) if path.endswith('locale.pak') else os.path.basename(path)
    english = lang.startswith('en') or '_en' in lang or 'en-' in lang
    strings += [(path, english, rid, text) for rid, text in pak_strings(path)]
  allowed_ids = {rid for _, english, rid, text in strings
                 if english and UPSTREAM.search(text) and not UPSTREAM.search(ALLOWED.sub('', text))}
  # An English string that names another product with Chrome: its translations may write that name their own way.
  chrome_ids = {rid for _, english, rid, text in strings if english and CHROME_KEPT.search(text)}
  for path, english, rid, text in strings:
    for what in problems_in(text, english, rid in allowed_ids, not english and rid in chrome_ids):
      hits.append(f'{os.path.relpath(path, app)} #{rid}: {what}: {text[:160]!r}')
  for path in plists:
    for text in plist_strings(path):
      for what in problems_in(text, True):
        hits.append(f'{os.path.relpath(path, app)}: {what}: {text[:160]!r}')
  return len(paks), len(plists), hits


def main():
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument('app')
  parser.add_argument('--pak', action='append', default=[])
  args = parser.parse_args()
  paks, plists, hits = scan(args.app, args.pak)
  for hit in hits:
    print(hit)
  print(f'brand_scan: {paks} .pak files, {plists} plists, {len(hits)} problems', file=sys.stderr)
  if paks == 0:
    print('brand_scan: no locale .pak files found', file=sys.stderr)
    sys.exit(1)
  sys.exit(1 if hits else 0)


if __name__ == '__main__':
  main()
