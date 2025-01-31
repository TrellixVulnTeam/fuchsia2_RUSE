#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import datetime
import errno
import os
import subprocess
import time

from device import Device
from host import Host
from log import Log


class Fuzzer(object):
  """Represents a Fuchsia fuzz target.

    This represents a binary fuzz target produced the Fuchsia build, referenced
    by a component manifest, and included in a fuzz package.  It provides an
    interface for running the fuzzer in different common modes, allowing
    specific command line arguments to libFuzzer to be abstracted.

    Attributes:
      device: A Device where this fuzzer can be run
      host: The build host that built the fuzzer
      pkg: The GN fuzzers_package name
      tgt: The GN fuzzers name
  """

  # Matches the prefixes in libFuzzer passed to |Fuzzer::DumpCurrentUnit| or
  # |Fuzzer::WriteUnitToFileWithPrefix|.
  ARTIFACT_PREFIXES = [
      'crash', 'leak', 'mismatch', 'oom', 'slow-unit', 'timeout'
  ]

  class NameError(ValueError):
    """Indicates a supplied name is malformed or unusable."""
    pass

  class StateError(ValueError):
    """Indicates a command isn't valid for the fuzzer in its current state."""
    pass

  @classmethod
  def filter(cls, fuzzers, name):
    """Filters a list of fuzzer names.

      Takes a list of fuzzer names in the form `pkg`/`tgt` and a name to filter
      on.  If the name is of the form 'x/y', the filtered list will include all
      the fuzzer names where 'x' is a substring of `pkg` and y is a substring
      of `tgt`; otherwise it includes all the fuzzer names where `name` is a
      substring of either `pkg` or `tgt`.

      Returns:
        A list of fuzzer names matching the given name.

      Raises:
        FuzzerNameError: Name is malformed, e.g. of the form 'x/y/z'.
    """
    if not name or name == '':
      return fuzzers
    names = name.split('/')
    if len(names) == 1:
      return list(
          set(Fuzzer.filter(fuzzers, '/' + name))
          | set(Fuzzer.filter(fuzzers, name + '/')))
    elif len(names) != 2:
      raise Fuzzer.NameError('Malformed fuzzer name: ' + name)
    filtered = []
    for pkg, tgt in fuzzers:
      if names[0] in pkg and names[1] in tgt:
        filtered.append((pkg, tgt))
    return filtered

  @classmethod
  def from_args(cls, device, args):
    """Constructs a Fuzzer from command line arguments."""
    fuzzers = Fuzzer.filter(device.host.fuzzers, args.name)
    if len(fuzzers) != 1:
      raise Fuzzer.NameError('Name did not resolve to exactly one fuzzer: \'' +
                             args.name + '\'. Try using \'list-fuzzers\'.')
    return cls(device, fuzzers[0][0], fuzzers[0][1], args.output,
               args.foreground)

  def __init__(self, device, pkg, tgt, output=None, foreground=False):
    self.device = device
    self.host = device.host
    self.pkg = pkg
    self.tgt = tgt
    if output:
      self._output = output
    else:
      self._output = self.host.join('test_data', 'fuzzing', self.pkg, self.tgt)
    self._foreground = foreground

  def __str__(self):
    return self.pkg + '/' + self.tgt

  def data_path(self, relpath=''):
    """Canonicalizes the location of mutable data for this fuzzer."""
    return '/data/r/sys/fuchsia.com:%s:0#meta:%s.cmx/%s' % (self.pkg, self.tgt,
                                                            relpath)

  def measure_corpus(self):
    """Returns the number of corpus elements and corpus size as a pair."""
    try:
      sizes = self.device.ls(self.data_path('corpus'))
      return (len(sizes), sum(sizes.values()))
    except subprocess.CalledProcessError:
      return (0, 0)

  def list_artifacts(self):
    """Returns a list of test unit artifacts, i.e. fuzzing crashes."""
    artifacts = []
    try:
      lines = self.device.ls(self.data_path())
      for file, _ in lines.iteritems():
        for prefix in Fuzzer.ARTIFACT_PREFIXES:
          if file.startswith(prefix):
            artifacts.append(file)
      return artifacts
    except subprocess.CalledProcessError:
      return []

  def is_running(self):
    """Checks the device and returns whether the fuzzer is running."""
    return self.tgt in self.device.getpids()

  def require_stopped(self):
    """Raise an exception if the fuzzer is running."""
    if self.is_running():
      raise Fuzzer.StateError(
          str(self) + ' is running and must be stopped first.')

  def results(self, relpath=None):
    """Returns the path in the previously prepared results directory."""
    if relpath:
      return os.path.join(self._output, 'latest', relpath)
    else:
      return os.path.join(self._output, 'latest')

  def url(self):
    return 'fuchsia-pkg://fuchsia.com/%s#meta/%s.cmx' % (self.pkg, self.tgt)

  def run(self, fuzzer_args, logfile=None):
    fuzz_cmd = ['run', self.url(), '-artifact_prefix=data'] + fuzzer_args
    print('+ ' + ' '.join(fuzz_cmd))
    self.device.ssh(fuzz_cmd, quiet=False, logfile=logfile)

  def start(self, fuzzer_args):
    """Runs the fuzzer.

      Executes a fuzzer in the "normal" fuzzing mode. It creates a log context,
      and waits after spawning the fuzzer until it completes. As a result,
      callers will typically want to run this in a background process.

      The command will be like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<tgt>.cmx \
        -artifact_prefix=data -jobs=1 data/corpus

      See also: https://llvm.org/docs/LibFuzzer.html#running

      Args:
        fuzzer_args: Command line arguments to pass to libFuzzer
    """
    self.require_stopped()
    results = os.path.join(self._output, datetime.datetime.utcnow().isoformat())
    try:
      os.unlink(self.results())
    except OSError as e:
      if e.errno != errno.ENOENT:
        raise
    try:
      os.makedirs(results)
    except OSError as e:
      if e.errno != errno.EEXIST:
        raise
    os.symlink(results, self.results())

    if len(filter(lambda x: x.startswith('-jobs='), fuzzer_args)) == 0:
      if self._foreground:
        fuzzer_args.append('-jobs=0')
      else:
        fuzzer_args.append('-jobs=1')
    self.device.ssh(['mkdir', '-p', self.data_path('corpus')])
    if len(filter(lambda x: not x.startswith('-'), fuzzer_args)) == 0:
      fuzzer_args.append('data/corpus')

    with Log(self):
      if self._foreground:
        self.run(fuzzer_args, logfile=self.results('fuzz-0.log'))
      else:
        self.run(fuzzer_args)
      while self.is_running():
        time.sleep(2)

  def stop(self):
    """Stops any processes with a matching component manifest on the device."""
    pids = self.device.getpids()
    if self.tgt in pids:
      self.device.ssh(['kill', str(pids[self.tgt])])

  def repro(self, fuzzer_args):
    """Runs the fuzzer with test input artifacts.

      Executes a command like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<tgt>.cmx \
        -artifact_prefix=data -jobs=1 data/<artifact>...

      See also: https://llvm.org/docs/LibFuzzer.html#options

      Returns: Number of test input artifacts found.
    """
    artifacts = self.list_artifacts()
    if len(artifacts) != 0:
      self.run(fuzzer_args + ['data/' + a for a in artifacts])
    return len(artifacts)

  def merge(self, fuzzer_args):
    """Attempts to minimizes the fuzzer's corpus.

      Executes a command like:
      run fuchsia-pkg://fuchsia.com/<pkg>#meta/<tgt>.cmx \
        -artifact_prefix=data -jobs=1 \
        -merge=1 -merge_control_file=data/.mergefile \
        data/corpus data/corpus.prev'

      See also: https://llvm.org/docs/LibFuzzer.html#corpus

      Returns: Same as measure_corpus
    """
    self.require_stopped()
    if self.measure_corpus() == (0, 0):
      return (0, 0)
    self.device.ssh(['mkdir', '-p', self.data_path('corpus')])
    self.device.ssh(['mkdir', '-p', self.data_path('corpus.prev')])
    self.device.ssh(
        ['mv', self.data_path('corpus/*'),
         self.data_path('corpus.prev')])
    self.device.ssh(['mkdir', '-p', self.data_path('corpus')])
    # Save mergefile in case we are interrupted
    fuzzer_args = ['-merge=1', '-merge_control_file=data/.mergefile'
                  ] + fuzzer_args
    fuzzer_args.append('data/corpus')
    fuzzer_args.append('data/corpus.prev')
    self.run(fuzzer_args)
    # Cleanup
    self.device.ssh(['rm', self.data_path('.mergefile')])
    self.device.ssh(['rm', '-r', self.data_path('corpus.prev')])
    return self.measure_corpus()
