/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_DBG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_DBG_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/commands.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/dbg-tlv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/error-dump.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/file.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/runtime.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-io.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-prph.h"

/**
 * struct iwl_fw_dump_desc - describes the dump
 * @len: length of trig_desc->data
 * @trig_desc: the description of the dump
 */
struct iwl_fw_dump_desc {
    size_t len;
    /* must be last */
    struct iwl_fw_error_dump_trigger_desc trig_desc;
};

/**
 * struct iwl_fw_dbg_params - register values to restore
 * @in_sample: DBGC_IN_SAMPLE value
 * @out_ctrl: DBGC_OUT_CTRL value
 */
struct iwl_fw_dbg_params {
    uint32_t in_sample;
    uint32_t out_ctrl;
};

extern const struct iwl_fw_dump_desc iwl_dump_desc_assert;

static inline void iwl_fw_free_dump_desc(struct iwl_fw_runtime* fwrt) {
    if (fwrt->dump.desc != &iwl_dump_desc_assert) { kfree((void*)fwrt->dump.desc); }
    fwrt->dump.desc = NULL;
    fwrt->dump.lmac_err_id[0] = 0;
    if (fwrt->smem_cfg.num_lmacs > 1) { fwrt->dump.lmac_err_id[1] = 0; }
    fwrt->dump.umac_err_id = 0;
}

void iwl_fw_error_dump(struct iwl_fw_runtime* fwrt);
int iwl_fw_dbg_collect_desc(struct iwl_fw_runtime* fwrt, const struct iwl_fw_dump_desc* desc,
                            bool monitor_only, unsigned int delay);
int _iwl_fw_dbg_collect(struct iwl_fw_runtime* fwrt, enum iwl_fw_dbg_trigger trig, const char* str,
                        size_t len, struct iwl_fw_dbg_trigger_tlv* trigger);
int iwl_fw_dbg_collect(struct iwl_fw_runtime* fwrt, uint32_t id, const char* str, size_t len);
int iwl_fw_dbg_collect_trig(struct iwl_fw_runtime* fwrt, struct iwl_fw_dbg_trigger_tlv* trigger,
                            const char* fmt, ...);
int iwl_fw_start_dbg_conf(struct iwl_fw_runtime* fwrt, uint8_t id);

#define iwl_fw_dbg_trigger_enabled(fw, id)                 \
    ({                                                     \
        void* __dbg_trigger = (fw)->dbg.trigger_tlv[(id)]; \
        unlikely(__dbg_trigger);                           \
    })

static inline struct iwl_fw_dbg_trigger_tlv* _iwl_fw_dbg_get_trigger(const struct iwl_fw* fw,
                                                                     enum iwl_fw_dbg_trigger id) {
    return fw->dbg.trigger_tlv[id];
}

#define iwl_fw_dbg_get_trigger(fw, id)            \
    ({                                            \
        BUILD_BUG_ON(!__builtin_constant_p(id));  \
        BUILD_BUG_ON((id) >= FW_DBG_TRIGGER_MAX); \
        _iwl_fw_dbg_get_trigger((fw), (id));      \
    })

static inline bool iwl_fw_dbg_trigger_vif_match(struct iwl_fw_dbg_trigger_tlv* trig,
                                                struct wireless_dev* wdev) {
    uint32_t trig_vif = le32_to_cpu(trig->vif_type);

    return trig_vif == IWL_FW_DBG_CONF_VIF_ANY || wdev->iftype == trig_vif;
}

static inline bool iwl_fw_dbg_trigger_stop_conf_match(struct iwl_fw_runtime* fwrt,
                                                      struct iwl_fw_dbg_trigger_tlv* trig) {
    return ((trig->mode & IWL_FW_DBG_TRIGGER_STOP) &&
            (fwrt->dump.conf == FW_DBG_INVALID ||
             (BIT(fwrt->dump.conf) & le32_to_cpu(trig->stop_conf_ids))));
}

#if 0   // NEEDS_PORTING
static inline bool iwl_fw_dbg_no_trig_window(struct iwl_fw_runtime* fwrt, uint32_t id,
                                             uint32_t dis_ms) {
    unsigned long wind_jiff = msecs_to_jiffies(dis_ms);

    /* If this is the first event checked, jump to update start ts */
    if (fwrt->dump.non_collect_ts_start[id] &&
        (time_after(fwrt->dump.non_collect_ts_start[id] + wind_jiff, jiffies))) {
        return true;
    }

    fwrt->dump.non_collect_ts_start[id] = jiffies;
    return false;
}

static inline bool iwl_fw_dbg_trigger_check_stop(struct iwl_fw_runtime* fwrt,
                                                 struct wireless_dev* wdev,
                                                 struct iwl_fw_dbg_trigger_tlv* trig) {
    if (wdev && !iwl_fw_dbg_trigger_vif_match(trig, wdev)) { return false; }

    if (iwl_fw_dbg_no_trig_window(fwrt, le32_to_cpu(trig->id), le16_to_cpu(trig->trig_dis_ms))) {
        IWL_WARN(fwrt, "Trigger %d occurred while no-collect window.\n", trig->id);
        return false;
    }

    return iwl_fw_dbg_trigger_stop_conf_match(fwrt, trig);
}

static inline struct iwl_fw_dbg_trigger_tlv* _iwl_fw_dbg_trigger_on(
    struct iwl_fw_runtime* fwrt, struct wireless_dev* wdev, const enum iwl_fw_dbg_trigger id) {
    struct iwl_fw_dbg_trigger_tlv* trig;

    if (fwrt->trans->ini_valid) { return NULL; }

    if (!iwl_fw_dbg_trigger_enabled(fwrt->fw, id)) { return NULL; }

    trig = _iwl_fw_dbg_get_trigger(fwrt->fw, id);

    if (!iwl_fw_dbg_trigger_check_stop(fwrt, wdev, trig)) { return NULL; }

    return trig;
}

#define iwl_fw_dbg_trigger_on(fwrt, wdev, id)         \
    ({                                                \
        BUILD_BUG_ON(!__builtin_constant_p(id));      \
        BUILD_BUG_ON((id) >= FW_DBG_TRIGGER_MAX);     \
        _iwl_fw_dbg_trigger_on((fwrt), (wdev), (id)); \
    })

static inline bool _iwl_fw_ini_trigger_on(struct iwl_fw_runtime* fwrt,
                                          const enum iwl_fw_dbg_trigger id) {
    struct iwl_fw_ini_active_triggers* trig = &fwrt->dump.active_trigs[id];
    uint32_t ms;

    if (!fwrt->trans->ini_valid) { return false; }

    if (!trig || !trig->active) { return false; }

    ms = le32_to_cpu(trig->conf->ignore_consec);
    if (ms) { ms /= USEC_PER_MSEC; }

    if (iwl_fw_dbg_no_trig_window(fwrt, id, ms)) {
        IWL_WARN(fwrt, "Trigger %d fired in no-collect window\n", id);
        return false;
    }

    return true;
}

#define iwl_fw_ini_trigger_on(fwrt, wdev, id)         \
    ({                                                \
        BUILD_BUG_ON(!__builtin_constant_p(id));      \
        BUILD_BUG_ON((id) >= IWL_FW_TRIGGER_ID_NUM);  \
        _iwl_fw_ini_trigger_on((fwrt), (wdev), (id)); \
    })

static inline void _iwl_fw_dbg_trigger_simple_stop(struct iwl_fw_runtime* fwrt,
                                                   struct wireless_dev* wdev,
                                                   struct iwl_fw_dbg_trigger_tlv* trigger) {
    if (!trigger) { return; }

    if (!iwl_fw_dbg_trigger_check_stop(fwrt, wdev, trigger)) { return; }

    iwl_fw_dbg_collect_trig(fwrt, trigger, NULL);
}

#define iwl_fw_dbg_trigger_simple_stop(fwrt, wdev, trig) \
    _iwl_fw_dbg_trigger_simple_stop((fwrt), (wdev), iwl_fw_dbg_get_trigger((fwrt)->fw, (trig)))

static int iwl_fw_dbg_start_stop_hcmd(struct iwl_fw_runtime* fwrt, bool start) {
    struct iwl_continuous_record_cmd cont_rec = {};
    struct iwl_host_cmd hcmd = {
        .id = LDBG_CONFIG_CMD,
        .flags = CMD_ASYNC,
        .data[0] = &cont_rec,
        .len[0] = sizeof(cont_rec),
    };

    cont_rec.record_mode.enable_recording =
        start ? cpu_to_le16(START_DEBUG_RECORDING) : cpu_to_le16(STOP_DEBUG_RECORDING);

    return iwl_trans_send_cmd(fwrt->trans, &hcmd);
}

static inline void _iwl_fw_dbg_stop_recording(struct iwl_trans* trans,
                                              struct iwl_fw_dbg_params* params) {
    if (trans->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
        iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
        return;
    }

    if (params) {
        params->in_sample = iwl_read_prph(trans, DBGC_IN_SAMPLE);
        params->out_ctrl = iwl_read_prph(trans, DBGC_OUT_CTRL);
    }

    iwl_write_prph(trans, DBGC_IN_SAMPLE, 0);
    udelay(100);
    iwl_write_prph(trans, DBGC_OUT_CTRL, 0);
#ifdef CPTCFG_IWLWIFI_DEBUGFS
    trans->dbg_rec_on = false;
#endif
}

static inline void iwl_fw_dbg_stop_recording(struct iwl_fw_runtime* fwrt,
                                             struct iwl_fw_dbg_params* params) {
    if (fwrt->trans->cfg->device_family < IWL_DEVICE_FAMILY_22560) {
        _iwl_fw_dbg_stop_recording(fwrt->trans, params);
    } else {
        iwl_fw_dbg_start_stop_hcmd(fwrt, false);
    }
}

static inline void _iwl_fw_dbg_restart_recording(struct iwl_trans* trans,
                                                 struct iwl_fw_dbg_params* params) {
    if (WARN_ON(!params)) { return; }

    if (trans->cfg->device_family == IWL_DEVICE_FAMILY_7000) {
        iwl_clear_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
        iwl_clear_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x1);
        iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x1);
    } else {
        iwl_write_prph(trans, DBGC_IN_SAMPLE, params->in_sample);
        udelay(100);
        iwl_write_prph(trans, DBGC_OUT_CTRL, params->out_ctrl);
    }
}

#ifdef CPTCFG_IWLWIFI_DEBUGFS
static inline void iwl_fw_set_dbg_rec_on(struct iwl_fw_runtime* fwrt) {
    if (fwrt->fw->dbg.dest_tlv && fwrt->cur_fw_img == IWL_UCODE_REGULAR) {
        fwrt->trans->dbg_rec_on = true;
    }
}
#endif

static inline void iwl_fw_dbg_restart_recording(struct iwl_fw_runtime* fwrt,
                                                struct iwl_fw_dbg_params* params) {
    if (fwrt->trans->cfg->device_family < IWL_DEVICE_FAMILY_22560) {
        _iwl_fw_dbg_restart_recording(fwrt->trans, params);
    } else {
        iwl_fw_dbg_start_stop_hcmd(fwrt, true);
    }
#ifdef CPTCFG_IWLWIFI_DEBUGFS
    iwl_fw_set_dbg_rec_on(fwrt);
#endif
}
#endif  // NEEDS_PORTING

static inline void iwl_fw_dump_conf_clear(struct iwl_fw_runtime* fwrt) {
    fwrt->dump.conf = FW_DBG_INVALID;
}

#if 0   // NEEDS_PORTING
void iwl_fw_error_dump_wk(struct work_struct* work);

static inline bool iwl_fw_dbg_type_on(struct iwl_fw_runtime* fwrt, uint32_t type) {
    return (fwrt->fw->dbg.dump_mask & BIT(type) || fwrt->trans->ini_valid);
}
#endif  // NEEDS_PORTING

static inline bool iwl_fw_dbg_is_d3_debug_enabled(struct iwl_fw_runtime* fwrt) {
    return false;
#if 0   // NEEDS_PORTING
    return fw_has_capa(&fwrt->fw->ucode_capa, IWL_UCODE_TLV_CAPA_D3_DEBUG) &&
           fwrt->trans->cfg->d3_debug_data_length &&
           iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_D3_DEBUG_DATA);
#endif  // NEEDS_PORTING
}

#if 0   // NEEDS_PORTING
static inline bool iwl_fw_dbg_is_paging_enabled(struct iwl_fw_runtime* fwrt) {
    return iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_PAGING) && !fwrt->trans->cfg->gen2 &&
           fwrt->fw->img[fwrt->cur_fw_img].paging_mem_size && fwrt->fw_paging_db[0].fw_paging_block;
}

void iwl_fw_dbg_read_d3_debug_data(struct iwl_fw_runtime* fwrt);

static inline void iwl_fw_flush_dump(struct iwl_fw_runtime* fwrt) {
    flush_delayed_work(&fwrt->dump.wk);
}

static inline void iwl_fw_cancel_dump(struct iwl_fw_runtime* fwrt) {
    cancel_delayed_work_sync(&fwrt->dump.wk);
}
#endif  // NEEDS_PORTING

#ifdef CPTCFG_IWLWIFI_DEBUGFS
static inline void iwl_fw_cancel_timestamp(struct iwl_fw_runtime* fwrt) {
    fwrt->timestamp.delay = 0;
    cancel_delayed_work_sync(&fwrt->timestamp.wk);
}

void iwl_fw_trigger_timestamp(struct iwl_fw_runtime* fwrt, uint32_t delay);

static inline void iwl_fw_suspend_timestamp(struct iwl_fw_runtime* fwrt) {
    cancel_delayed_work_sync(&fwrt->timestamp.wk);
}

static inline void iwl_fw_resume_timestamp(struct iwl_fw_runtime* fwrt) {
    if (!fwrt->timestamp.delay) { return; }

    schedule_delayed_work(&fwrt->timestamp.wk, round_jiffies_relative(fwrt->timestamp.delay));
}

#else

static inline void iwl_fw_cancel_timestamp(struct iwl_fw_runtime* fwrt) {}

static inline void iwl_fw_trigger_timestamp(struct iwl_fw_runtime* fwrt, uint32_t delay) {}

static inline void iwl_fw_suspend_timestamp(struct iwl_fw_runtime* fwrt) {}

static inline void iwl_fw_resume_timestamp(struct iwl_fw_runtime* fwrt) {}

#endif /* CPTCFG_IWLWIFI_DEBUGFS */

void iwl_fw_assert_error_dump(struct iwl_fw_runtime* fwrt);
void iwl_fw_alive_error_dump(struct iwl_fw_runtime* fwrt);
void iwl_fw_dbg_collect_sync(struct iwl_fw_runtime* fwrt);
void iwl_fw_dbg_apply_point(struct iwl_fw_runtime* fwrt, enum iwl_fw_ini_apply_point apply_point);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_DBG_H_
