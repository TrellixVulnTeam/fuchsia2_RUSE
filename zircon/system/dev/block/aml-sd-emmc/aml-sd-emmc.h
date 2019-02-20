// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>
#include <ddk/io-buffer.h>
#include <ddk/phys-iter.h>
#include <lib/mmio/mmio.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/sdmmc.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sdmmc.h>
#include <lib/sync/completion.h>
#include <lib/zx/interrupt.h>
#include <zircon/thread_annotations.h>
#include <soc/aml-common/aml-sd-emmc.h>

namespace sdmmc {

class AmlSdEmmc;
using AmlSdEmmcType = ddk::Device<AmlSdEmmc, ddk::Unbindable>;

class AmlSdEmmc : public AmlSdEmmcType,
                  public ddk::SdmmcProtocol<AmlSdEmmc, ddk::base_protocol> {
public:
    explicit AmlSdEmmc(zx_device_t* parent,
                       const ddk::PDev& pdev,
                       zx::bti bti,
                       ddk::MmioBuffer mmio,
                       ddk::MmioPinnedBuffer pinned_mmio,
                       aml_sd_emmc_config_t config,
                       zx::interrupt irq,
                       const ddk::GpioProtocolClient& gpio)
        : AmlSdEmmcType(parent), pdev_(pdev), bti_(std::move(bti)),
          mmio_(std::move(mmio)), pinned_mmio_(std::move(pinned_mmio)),
          reset_gpio_(gpio), irq_(std::move(irq)), board_config_(config){}

    static zx_status_t Create(void* ctx, zx_device_t* parent);

    // Device protocol implementation
    void DdkRelease();
    void DdkUnbind();

    // Sdmmc Protocol implementation
    zx_status_t SdmmcHostInfo(sdmmc_host_info_t* out_info);
    zx_status_t SdmmcSetSignalVoltage(sdmmc_voltage_t voltage);
    zx_status_t SdmmcSetBusWidth(sdmmc_bus_width_t bus_width);
    zx_status_t SdmmcSetBusFreq(uint32_t bus_freq);
    zx_status_t SdmmcSetTiming(sdmmc_timing_t timing);
    void SdmmcHwReset();
    zx_status_t SdmmcPerformTuning(uint32_t cmd_idx);
    zx_status_t SdmmcRequest(sdmmc_req_t* req);

private:
    void aml_sd_emmc_dump_regs() const;
    void aml_sd_emmc_dump_status(uint32_t status) const;
    void aml_sd_emmc_dump_cfg(uint32_t config) const;
    void aml_sd_emmc_dump_clock(uint32_t clock) const;
    void aml_sd_emmc_dump_desc_cmd_cfg(uint32_t cmd_desc) const;
    uint32_t get_clk_freq(uint32_t clk_src) const;
    zx_status_t aml_sd_emmc_do_tuning_transfer(uint8_t* tuning_res,
                                               uint16_t blk_pattern_size,
                                               uint32_t tuning_cmd_idx);
    bool aml_sd_emmc_tuning_test_delay(const uint8_t* blk_pattern,
                                       uint16_t blk_pattern_size, uint32_t adj_delay,
                                       uint32_t tuning_cmd_idx);
    zx_status_t aml_sd_emmc_tuning_calculate_best_window(const uint8_t* tuning_blk,
                                                         uint16_t tuning_blk_size,
                                                         uint32_t cur_clk_div, int* best_start,
                                                         uint32_t* best_size,
                                                         uint32_t tuning_cmd_idx);
    void aml_sd_emmc_init_regs();
    void aml_sd_emmc_setup_cmd_desc(sdmmc_req_t* req,
                                    aml_sd_emmc_desc_t** out_desc);
    zx_status_t aml_sd_emmc_setup_data_descs_dma(sdmmc_req_t* req,
                                                 aml_sd_emmc_desc_t* cur_desc,
                                                 aml_sd_emmc_desc_t** last_desc);
    zx_status_t aml_sd_emmc_setup_data_descs_pio(sdmmc_req_t* req,
                                                 aml_sd_emmc_desc_t* desc,
                                                 aml_sd_emmc_desc_t** last_desc);
    zx_status_t aml_sd_emmc_setup_data_descs(sdmmc_req_t *req,
                                             aml_sd_emmc_desc_t *desc,
                                             aml_sd_emmc_desc_t **last_desc);
    zx_status_t aml_sd_emmc_finish_req(sdmmc_req_t* req);
    int IrqThread();
    zx_status_t Bind();
    zx_status_t Init();

    ddk::PDev pdev_;
    zx::bti bti_;

    //std::optional<ddk::MmioBuffer> mmio_;
    ddk::MmioBuffer mmio_;
    ddk::MmioPinnedBuffer pinned_mmio_;
    const ddk::GpioProtocolClient reset_gpio_;
    zx::interrupt irq_;
    const aml_sd_emmc_config_t board_config_;
    mmio_buffer_t mymmio_;

    thrd_t irq_thread_;
    sdmmc_host_info_t dev_info_;
    io_buffer_t descs_buffer_;
    sync_completion_t req_completion_;
    // virt address of mmio_
    aml_sd_emmc_regs_t* regs_;
    // Held when I/O submit/complete is in progress.
    mtx_t mtx_;
    // cur pending req
    sdmmc_req_t* cur_req_ TA_GUARDED(mtx_);
    uint32_t max_freq_, min_freq_;
};

} //namespace sdmmc
