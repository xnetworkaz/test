#!/usr/bin/env python

# Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.


import json
import re


def CheckThirdPartyDirectory(input_api, output_api):
  # We have to put something in black_list here that won't blacklist
  # third_party/* because otherwise default black list will be used. Default
  # list contains third_party, so source set will become empty.
  third_party_sources = lambda x: (
    input_api.FilterSourceFile(x, white_list=(r'^third_party[\\\/].+',),
                               black_list=(r'^_',)))

  webrtc_owned_deps_list_path = input_api.os_path.join(
      input_api.PresubmitLocalPath(),
      'THIRD_PARTY_WEBRTC_DEPS.json')
  chromium_owned_deps_list_path = input_api.os_path.join(
      input_api.PresubmitLocalPath(),
      'THIRD_PARTY_CHROMIUM_DEPS.json')
  webrtc_owned_deps = _LoadDepsList(webrtc_owned_deps_list_path)
  chromium_owned_deps = _LoadDepsList(chromium_owned_deps_list_path)
  chromium_added_deps = _GetChromiumOwnedAddedDeps(input_api)

  results = []
  results.extend(CheckNoNotOwned3ppDeps(input_api, output_api,
                                        webrtc_owned_deps, chromium_owned_deps,
                                        third_party_sources))
  results.extend(CheckNoBothOwned3ppDeps(output_api, webrtc_owned_deps,
                                         chromium_owned_deps))
  results.extend(CheckNoChangesInAutoImportedDeps(input_api, output_api,
                                                  webrtc_owned_deps,
                                                  chromium_owned_deps,
                                                  chromium_added_deps,
                                                  third_party_sources))
  return results


def _GetChromiumOwnedAddedDeps(input_api):
  """Return list of deps that were added into chormium owned deps list."""

  chromium_owned_deps_list_source = lambda x: (
    input_api.FilterSourceFile(x,
                               white_list=('THIRD_PARTY_CHROMIUM_DEPS.json',),
                               black_list=(r'^_',)))

  added_deps = []
  for f in input_api.AffectedFiles(file_filter=chromium_owned_deps_list_source):
    print 'Loading chromium owned deps changes from %s' % f.LocalPath()
    if f.Action() != 'M':
      break

    prev_json = json.loads(_ReadAffectedFileContent(f.OldContents()))
    new_json = json.loads(_ReadAffectedFileContent(f.NewContents()))
    prev_deps_set = set(prev_json.get('dependencies', []))
    new_deps_set = set(new_json.get('dependencies', []))
    added_deps = list(new_deps_set.difference(prev_deps_set))
    break

  return added_deps


def _ReadAffectedFileContent(line_iter):
  content = ''
  for line in line_iter:
    content = '%s%s\n' % (content, line)
  return content


def CheckNoNotOwned3ppDeps(input_api, output_api,
    webrtc_owned_deps, chromium_owned_deps, third_party_sources):
  """Checks that there are no any not owned third_party deps."""
  error_msg = ('Third party dependency [{}] have to be specified either in '
               'THIRD_PARTY_WEBRTC_DEPS.json or in '
               'THIRD_PARTY_CHROMIUM_DEPS.json.\n'
               'If you want to add chromium-specific'
               'dependency you can run this command (better in separate CL): \n'
               './tools_webrtc/autoroller/checkin_chromium_dep.py -d {}\n'
               'If you want to add WebRTC-specific dependency just add it into '
               'THIRD_PARTY_WEBRTC_DEPS.json manually')

  not_owned_deps = set()
  results = []
  for f in input_api.AffectedFiles(file_filter=third_party_sources):
    file_path = f.LocalPath()
    split = re.split(r'[\\\/]', file_path)
    dep_name = split[1]
    if (dep_name not in not_owned_deps
        and dep_name not in webrtc_owned_deps
        and dep_name not in chromium_owned_deps):
      results.append(
          output_api.PresubmitError(error_msg.format(dep_name, dep_name)))
      not_owned_deps.add(dep_name)
  return results


def CheckNoBothOwned3ppDeps(output_api, webrtc_owned_deps, chromium_owned_deps):
  """Checks that there are no any not owned third_party deps."""
  error_msg = ('Third party dependencies {} can\'t be a WebRTC- and '
               'Chromium-specific dependency at the same time. '
               'Remove them from one of these files: '
               'THIRD_PARTY_WEBRTC_DEPS.json or THIRD_PARTY_CHROMIUM_DEPS.json')

  both_owned_deps = set(chromium_owned_deps).intersection(
      set(webrtc_owned_deps))
  results = []
  if both_owned_deps:
    results.append(output_api.PresubmitError(error_msg.format(
        json.dumps(list(both_owned_deps)))))
  return results


def CheckNoChangesInAutoImportedDeps(input_api, output_api,
    webrtc_owned_deps, chromium_owned_deps, chromium_added_deps,
    third_party_sources):
  """Checks that there are no changes in deps imported by autoroller."""

  error_msg = ('Changes in [{}] will be overridden during chromium third_party '
               'autoroll. If you really want to change this code you have to '
               'do it upstream in Chromium\'s third_party.')
  results = []
  for f in input_api.AffectedFiles(file_filter=third_party_sources):
    file_path = f.LocalPath()
    split = re.split(r'[\\\/]', file_path)
    dep_name = split[1]
    if (dep_name not in webrtc_owned_deps
        and dep_name in chromium_owned_deps
        and dep_name not in chromium_added_deps):
      results.append(output_api.PresubmitError(error_msg.format(file_path)))
  return results


def _LoadDepsList(file_name):
  with open(file_name, 'rb') as f:
    content = json.load(f)
    return content.get('dependencies', [])
