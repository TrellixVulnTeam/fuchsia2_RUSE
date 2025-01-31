// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/tests/zx_object_util.h"

#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <vector>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace feedback {
namespace {

std::vector<zx_koid_t> GetChildKoids(const zx_handle_t parent,
                                     zx_object_info_topic_t child_kind) {
  std::vector<zx_koid_t> result(100);  // 100 ought to be enough for tests.
  size_t actual = 0;
  size_t available = 0;
  FXL_CHECK(zx_object_get_info(parent, child_kind, result.data(),
                               result.size() * sizeof(result[0]), &actual,
                               &available) == ZX_OK);
  FXL_CHECK(actual == available);
  result.resize(actual);
  return result;
}

template <typename ResultObject>
std::vector<ResultObject> GetChildObjects(const zx_handle_t parent,
                                          uint32_t child_kind) {
  auto koids = GetChildKoids(parent, child_kind);

  std::vector<ResultObject> result;
  result.reserve(koids.size());
  for (const auto& koid : koids) {
    zx_handle_t handle;
    FXL_CHECK(zx_object_get_child(parent, koid, ZX_RIGHT_SAME_RIGHTS,
                                  &handle) == ZX_OK);
    result.push_back(ResultObject(handle));
  }
  return result;
}

}  // namespace

std::vector<zx::job> GetChildJobs(const zx_handle_t job) {
  return GetChildObjects<zx::job>(job, ZX_INFO_JOB_CHILDREN);
}

std::vector<zx::process> GetChildProcesses(const zx_handle_t job) {
  return GetChildObjects<zx::process>(job, ZX_INFO_JOB_PROCESSES);
}

}  // namespace feedback
}  // namespace fuchsia
