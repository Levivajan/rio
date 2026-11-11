#!/usr/bin/env python3
# Copyright 2026 The Rio Authors. All rights reserved.
"""Copies the WebUI bundles and their .grd files (npm run build:webui) into the build's gen dir, so grit finds them
under <(SHARED_INTERMEDIATE_DIR)/clu/resources/webui, the key tools/gritsettings/resource_ids.spec assigns their ids
by. Usage: copy_webui.py <bundle dir> <out dir> <grd name>..."""

import os
import shutil
import sys


def main(argv):
  src, out, grds = argv[1], argv[2], argv[3:]
  for grd in grds:
    if not os.path.isfile(os.path.join(src, grd)):
      sys.stderr.write('%s is missing; run npm run build:webui\n' % os.path.join(src, grd))
      return 1
  if os.path.isdir(out):
    shutil.rmtree(out)
  shutil.copytree(src, out)
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv))
