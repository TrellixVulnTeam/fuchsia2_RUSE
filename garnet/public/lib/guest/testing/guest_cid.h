// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file is being moved to //sdk/lib/virtualization.
// New users should prefer files there instead.

#ifndef LIB_GUEST_TESTING_GUEST_CID_H_
#define LIB_GUEST_TESTING_GUEST_CID_H_

namespace guest {
namespace testing {

// This is typically '3' in production for the first guest. Use some alternate
// value here to ensure we don't couple too tightly to that behavior.
static constexpr uint32_t kGuestCid = 1234;

}  // namespace testing
}  // namespace guest

#endif  // LIB_GUEST_TESTING_GUEST_CID_H_
