// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Standard Includes
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

// DDK includes
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddktl/pdev.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/usb/dci.h>
#include <ddktl/protocol/usb.h>
#include <fbl/mutex.h>
#include <lib/mmio/mmio.h>
#include <usb/request-cpp.h>

// Zircon USB includes
#include <zircon/hw/usb.h>

#include "usb_dwc_regs.h"

namespace dwc2 {

//#define SINGLE_EP_IN_QUEUE 1

class Dwc2;
using Dwc2Type = ddk::Device<Dwc2, ddk::Unbindable>;

class Dwc2 : public Dwc2Type, public ddk::UsbDciProtocol<Dwc2, ddk::base_protocol> {
public:
    explicit Dwc2(zx_device_t* parent, pdev_protocol_t* pdev)
        : Dwc2Type(parent), pdev_(pdev) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent);
    zx_status_t Init();
    int IrqThread();

    // Device protocol implementation.
    void DdkUnbind();
    void DdkRelease();

    // USB DCI protocol implementation.
     void UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_t* cb);
     zx_status_t UsbDciSetInterface(const usb_dci_interface_protocol_t* interface);
     zx_status_t UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc, const
                                usb_ss_ep_comp_descriptor_t* ss_comp_desc);
     zx_status_t UsbDciDisableEp(uint8_t ep_address);
     zx_status_t UsbDciEpSetStall(uint8_t ep_address);
     zx_status_t UsbDciEpClearStall(uint8_t ep_address);
     size_t UsbDciGetRequestSize();
     zx_status_t UsbDciCancelAll(uint8_t ep_address);

private:
    enum class Ep0State {
        DISCONNECTED,
        IDLE,
        DATA_OUT,
        DATA_IN,
        STATUS,
        STALL,
    };

    using Request = usb::UnownedRequest<void>;
    using RequestQueue = usb::UnownedRequestQueue<void>;

    struct Endpoint {
        // Requests waiting to be processed.
        RequestQueue queued_reqs __TA_GUARDED(lock);
        // request currently being processed.
        usb_request_t* current_req __TA_GUARDED(lock) = nullptr;

        uint8_t* req_buffer;
        uint32_t req_offset;
        uint32_t req_length;    
    
        // Used for synchronizing endpoint state
        // and ep specific hardware registers
        // This should be acquired before Dwc2.lock_
        // if acquiring both locks.
        fbl::Mutex lock;
    
        uint16_t max_packet_size;
        uint8_t ep_num;
        bool enabled;
        uint8_t type;           // control, bulk, interrupt or isochronous
        uint8_t interval;
        bool send_zlp;
        bool stalled;
    };

    DISALLOW_COPY_ASSIGN_AND_MOVE(Dwc2);

    void FlushFifo(uint32_t fifo_num);
    zx_status_t InitController();
    zx_status_t Start();
    void StartEp0();
    void ReadPacket(void* buffer, uint32_t length, uint8_t ep_num);
    bool WritePacket(uint8_t ep_num);
    void StartEndpoints();
    void StopEndpoints();
    void HandleEp0Setup();
    void Ep0StartOut();
    void HandleEp0Status(bool is_in);
    void CompleteEp0();
    void HandleEp0();
    void EpComplete(uint8_t ep_num);
    void EndTransfers(uint8_t ep_num, zx_status_t reason);
    zx_status_t SetStall(uint8_t ep_num, bool stall);
    void EnableEp(uint8_t ep_num, bool enable);
    void EpQueueNextLocked(Endpoint* ep);
    void StartTransfer(uint8_t ep_num, uint32_t length);

    // Interrupts
    void HandleReset();
    void HandleSuspend();
    void HandleEnumDone();
    void HandleRxStatusQueueLevel();
    void HandleInEpInterrupt();
    void HandleOutEpInterrupt();
    void HandleTxFifoEmpty();

    zx_status_t HandleSetup(size_t* out_actual);
    void SetAddress(uint8_t address);

    inline ddk::MmioBuffer* get_mmio() {
        return &*mmio_;
    }

    Endpoint endpoints_[DWC_MAX_EPS];

#if SINGLE_EP_IN_QUEUE
    RequestQueue queued_in_reqs_ __TA_GUARDED(lock);
    usb_request_t* current_in_req_;
#endif

    // Used for synchronizing global state
    // and non ep specific hardware registers.
    // Endpoint.lock should be acquired first
    // if acquiring both locks.
    fbl::Mutex lock_;

    bool configured_ = false;

    usb_setup_t cur_setup_ = {};   
    Ep0State ep0_state_ = Ep0State::DISCONNECTED;
    uint8_t ep0_buffer_[UINT16_MAX];
    bool got_setup_ = false;

    ddk::PDev pdev_;
    std::optional<ddk::UsbDciInterfaceProtocolClient> dci_intf_;

    std::optional<ddk::MmioBuffer> mmio_;

    zx::interrupt irq_;
    thrd_t irq_thread_;
};

} // namespace dwc2

