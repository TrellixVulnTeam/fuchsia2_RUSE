# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
# DO NOT MANUALLY EDIT!
# Generated by //scripts/sdk/bazel/create_bazel_layout.py.

package(default_visibility = ["//visibility:public"])
licenses(["unencumbered"])

exports_files([
  % for tool in sorted(data):
  "${tool}",
  % endfor
])