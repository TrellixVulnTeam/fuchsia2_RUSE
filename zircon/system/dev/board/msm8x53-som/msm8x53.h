// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>

namespace board_msm8x53 {

// BTI IDs
enum {
    BTI_SDC1,
};

class Msm8x53;
using Msm8x53Type = ddk::Device<Msm8x53>;

// This is the main class for the platform bus driver.
class Msm8x53 : public Msm8x53Type {
public:
    explicit Msm8x53(zx_device_t* parent, pbus_protocol_t* pbus, pdev_board_info_t* board_info)
        : Msm8x53Type(parent), pbus_(pbus), board_info_(*board_info) {}

    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    void DdkRelease();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Msm8x53);

    zx_status_t Start();
    zx_status_t GpioInit();
    zx_status_t Sdc1Init();
    int Thread();

    ddk::PBusProtocolClient pbus_;
    pdev_board_info_t board_info_;
    thrd_t thread_;
};

} // namespace board_msm8x53
