#!/usr/bin/env vpython
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file was copy-pasted over from:
# //build/scripts/slave/upload_perf_dashboard_results.py
# with sections copied from:
# //build/scripts/slave/slave_utils.py

import json
import optparse
import re
import sys
import urllib

from core import results_dashboard


RESULTS_LINK_PATH = '/report?masters=%s&bots=%s&tests=%s&rev=%s'


def _GetMainRevision(commit_pos):
  """Return revision to use as the numerical x-value in the perf dashboard.
  This will be used as the value of "rev" in the data passed to
  results_dashboard.SendResults.
  This function returns the value of "got_revision_cp" in build properties.
  """
  return int(re.search(r'{#(\d+)}', commit_pos).group(1))


def _GetDashboardJson(options):
  main_revision = _GetMainRevision(options.got_revision_cp)
  revisions = _GetPerfDashboardRevisionsWithProperties(
    options.got_webrtc_revision, options.got_v8_revision, options.version,
    options.git_revision, main_revision)
  reference_build = 'reference' in options.name
  stripped_test_name = options.name.replace('.reference', '')
  results = {}
  print 'Opening results file %s' % options.results_file
  with open(options.results_file) as f:
    results = json.load(f)
  dashboard_json = {}
  if not 'charts' in results:
    # These are legacy results.
    dashboard_json = results_dashboard.MakeListOfPoints(
      results, options.configuration_name, stripped_test_name,
      options.buildername, options.buildnumber, {},
      _GetMachineGroup(options.perf_dashboard_machine_group,
          options.is_luci_builder), revisions_dict=revisions)
  else:
    dashboard_json = results_dashboard.MakeDashboardJsonV1(
      results,
      revisions, stripped_test_name, options.configuration_name,
      options.buildername, options.buildnumber,
      {}, reference_build,
      perf_dashboard_machine_group=_GetMachineGroup(
          options.perf_dashboard_machine_group, options.is_luci_builder))
  return dashboard_json

def _GetMachineGroup(perf_dashboard_machine_group, is_luci_builder):
  if is_luci_builder and not perf_dashboard_machine_group:
    raise ValueError(
        "Luci builder must set 'perf_dashboard_machine_group'. See "
        'bit.ly/perf-dashboard-machine-group for more details')
  elif not is_luci_builder:
    # TODO(crbug.com/801289):
    # Remove this code path once all builders are converted to LUCI.
    # perf_dashboard_machine_group = chromium_utils.GetActiveMaster()
    # hardcoding the result of this line for now
    perf_dashboard_machine_group = 'ChromiumPerfFyi'
  return perf_dashboard_machine_group


def _GetDashboardHistogramData(options):
  revisions = {
      '--chromium_commit_positions': _GetMainRevision(options.got_revision_cp),
      '--chromium_revisions': options.git_revision
  }

  if options.got_webrtc_revision:
    revisions['--webrtc_revisions'] = options.got_webrtc_revision
  if options.got_v8_revision:
    revisions['--v8_revisions'] = options.got_v8_revision

  is_reference_build = 'reference' in options.name
  stripped_test_name = options.name.replace('.reference', '')

  return results_dashboard.MakeHistogramSetWithDiagnostics(
      options.results_file, stripped_test_name,
      options.configuration_name, options.buildername, options.buildnumber,
      revisions, is_reference_build,
      perf_dashboard_machine_group=_GetMachineGroup(
          options.perf_dashboard_machine_group, options.is_luci_builder))


def _CreateParser():
  # Parse options
  parser = optparse.OptionParser()
  parser.add_option('--name')
  parser.add_option('--results-file')
  parser.add_option('--tmp-dir')
  parser.add_option('--output-json-file')
  parser.add_option('--got-revision-cp')
  parser.add_option('--configuration-name')
  parser.add_option('--results-url')
  parser.add_option('--is-luci-builder', action='store_true', default=False)
  parser.add_option('--perf-dashboard-machine-group')
  parser.add_option('--buildername')
  parser.add_option('--buildnumber')
  parser.add_option('--got-webrtc-revision')
  parser.add_option('--got-v8-revision')
  parser.add_option('--version')
  parser.add_option('--git-revision')
  parser.add_option('--output-json-dashboard-url')
  parser.add_option('--send-as-histograms', action='store_true')
  parser.add_option('--oauth-token-file')
  return parser


def main(args):
  parser = _CreateParser()
  options, extra_args = parser.parse_args(args)

  # Validate options.
  if extra_args:
    parser.error('Unexpected command line arguments')
  if not options.configuration_name or not options.results_url:
    parser.error('configuration_name and results_url are required.')

  if options.oauth_token_file:
    with open(options.oauth_token_file) as f:
      oauth_token = f.readline()
  else:
    oauth_token = None

  if not options.send_as_histograms:
    dashboard_json = _GetDashboardJson(options)
  else:
    dashboard_json = _GetDashboardHistogramData(options)

  if dashboard_json:
    if options.output_json_file:
      json.dump(dashboard_json, options.output_json_file,
          indent=4, separators=(',', ': '))

    if options.output_json_dashboard_url:
      # Dump dashboard url to file.
      dashboard_url = GetDashboardUrl(options.name,
          options.configuration_name, options.results_url,
          options.got_revision_cp,
          options.perf_dashboard_machine_group,
          options.is_luci_builder)
      with open(options.output_json_dashboard_url, 'w') as f:
        json.dump(dashboard_url if dashboard_url else '', f)

    if not results_dashboard.SendResults(
        dashboard_json,
        options.results_url,
        options.tmp_dir,
        send_as_histograms=options.send_as_histograms,
        oauth_token=oauth_token):
      return 1
  else:
    print 'Error: No perf dashboard JSON was produced.'
    print '@@@STEP_FAILURE@@@'
    return 1
  return 0

if __name__ == '__main__':
  sys.exit(main((sys.argv[1:])))


def GetDashboardUrl(name, configuration_name, results_url,
    got_revision_cp, perf_dashboard_machine_group=None,
    is_luci_builder=False):
  """Optionally writes the dashboard url to a file
    and returns a link to the dashboard.
  """
  name = name.replace('.reference', '')
  dashboard_url = results_url + RESULTS_LINK_PATH % (
      urllib.quote(_GetMachineGroup(perf_dashboard_machine_group,
          is_luci_builder)),
      urllib.quote(configuration_name),
      urllib.quote(name),
      _GetMainRevision(got_revision_cp))

  return dashboard_url


def _GetPerfDashboardRevisionsWithProperties(
    got_webrtc_revision, got_v8_revision, version, git_revision, main_revision,
    point_id=None):
  """Fills in the same revisions fields that process_log_utils does."""

  versions = {}
  versions['rev'] = main_revision
  versions['webrtc_git'] = got_webrtc_revision
  versions['v8_rev'] = got_v8_revision
  versions['ver'] = version
  versions['git_revision'] = git_revision
  versions['point_id'] = point_id
  # There are a lot of "bad" revisions to check for, so clean them all up here.
  for key in versions.keys():
    if not versions[key] or versions[key] == 'undefined':
      del versions[key]
  return versions
