#!/usr/bin/env python3
# Copyright 2026 The Rio Authors. All rights reserved.
"""Builds rio-ai (clu_ai_helper.swift) with the SDK's swiftc: arm64, macOS 26 (Foundation Models)."""

import subprocess
import sys


def main(argv):
  if len(argv) != 3:
    print('usage: build_ai_helper.py <source.swift> <output>', file=sys.stderr)
    return 2
  source, output = argv[1], argv[2]
  return subprocess.call([
      'xcrun', '--sdk', 'macosx', 'swiftc', '-O', '-target',
      'arm64-apple-macos26.0', '-o', output, source
  ])


if __name__ == '__main__':
  sys.exit(main(sys.argv))
