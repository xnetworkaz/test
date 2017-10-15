#!/usr/bin/env python
# Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

"""Downloads precompiled tools.

These are checked into the repository as SHA-1 hashes (see *.sha1 files in
subdirectories). Note that chrome-webrtc-resources is a Google-internal bucket,
so please download and compile these tools manually if this script fails.
"""

import os
import sys


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir))
DEPOT_TOOLS_DIR = os.path.join(SRC_DIR, 'third_party', 'depot_tools')
sys.path.insert(0, DEPOT_TOOLS_DIR)


import gclient_utils
import subprocess2


def main(directories):
  if not directories:
    directories = [SCRIPT_DIR]

  for path in directories:
    cmd = [
      sys.executable,
      os.path.join(DEPOT_TOOLS_DIR, 'download_from_google_storage.py'),
      '--directory',
      '--num_threads=10',
      '--bucket', 'chrome-webrtc-resources',
      '--auto_platform',
      '--recursive',
      path,
    ]
    print 'Downloading precompiled tools...'

    # Perform download similar to how gclient hooks execute.
    try:
      gclient_utils.CheckCallAndFilterAndHeader(cmd, cwd=SRC_DIR, always=True)
    except (gclient_utils.Error, subprocess2.CalledProcessError) as e:
      print 'Error: %s' % str(e)
      return 2
    return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
