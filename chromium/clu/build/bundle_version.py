#!/usr/bin/env python3
# Copyright 2026 The Rio Authors. All rights reserved.
"""Copies the app's tweaked Info.plist and sets CFBundleVersion to this build's version.

Usage: bundle_version.py <in.plist> <out.plist> <version> [<bundle id>]. An empty version keeps Chromium's. A bundle id
replaces the app's (out/Dev's ends in .test, so a test build never shares the installed Rio's id and macOS never
hands it the user's links); the helpers keep theirs. The short version
(CFBundleShortVersionString) stays Chromium's, so About still says 158.0.8127.18.
"""

import plistlib
import re
import sys


def main(argv):
    if len(argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 1
    src, dst, version = argv[1:4]
    bundle_id = argv[4] if len(argv) == 5 else ''
    if bundle_id and not re.fullmatch(r'[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+', bundle_id):
        print(f'bad CFBundleIdentifier {bundle_id!r}', file=sys.stderr)
        return 1
    if version and not re.fullmatch(r'[0-9]{1,9}(\.[0-9]{1,9}){0,4}', version):
        print(f'bad CFBundleVersion {version!r}: digits and dots, at most five parts', file=sys.stderr)
        return 1
    with open(src, 'rb') as f:
        plist = plistlib.load(f)
    if version:
        plist['CFBundleVersion'] = version
    if bundle_id:
        plist['CFBundleIdentifier'] = bundle_id
    with open(dst, 'wb') as f:
        plistlib.dump(plist, f, fmt=plistlib.FMT_XML)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
