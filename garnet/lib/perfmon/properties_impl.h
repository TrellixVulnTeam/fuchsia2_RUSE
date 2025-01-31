// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_PERFMON_PROPERTIES_IMPL_H_
#define GARNET_LIB_PERFMON_PROPERTIES_IMPL_H_

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>

#include "garnet/lib/perfmon/properties.h"

namespace perfmon {
namespace internal {

void IoctlToPerfmonProperties(const perfmon_ioctl_properties_t& props,
                              Properties* out_props);

}  // namespace internal
}  // namespace perfmon

#endif  // GARNET_LIB_PERFMON_PROPERTIES_IMPL_H_
