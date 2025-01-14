/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef BRCMFMAC_BUS_H
#define BRCMFMAC_BUS_H

#include <ddk/protocol/composite.h>
#include <ddk/protocol/usb.h>

#include "debug.h"
#include "netbuf.h"

// clang-format off
/* IDs of the 6 default common rings of msgbuf protocol */
#define BRCMF_H2D_MSGRING_CONTROL_SUBMIT    0
#define BRCMF_H2D_MSGRING_RXPOST_SUBMIT     1
#define BRCMF_H2D_MSGRING_FLOWRING_IDSTART  2
#define BRCMF_D2H_MSGRING_CONTROL_COMPLETE  2
#define BRCMF_D2H_MSGRING_TX_COMPLETE       3
#define BRCMF_D2H_MSGRING_RX_COMPLETE       4

#define BRCMF_NROF_H2D_COMMON_MSGRINGS      2
#define BRCMF_NROF_D2H_COMMON_MSGRINGS      3
#define BRCMF_NROF_COMMON_MSGRINGS (BRCMF_NROF_H2D_COMMON_MSGRINGS + BRCMF_NROF_D2H_COMMON_MSGRINGS)
// clang-format on

/* The level of bus communication with the dongle */
enum brcmf_bus_state {
    BRCMF_BUS_DOWN, /* Not ready for frame transfers */
    BRCMF_BUS_UP    /* Ready for frame transfers */
};

/* The level of bus communication with the dongle */
enum brcmf_bus_protocol_type { BRCMF_PROTO_BCDC, BRCMF_PROTO_MSGBUF };

struct brcmf_mp_device;

struct brcmf_bus_dcmd {
    const char* name;
    char* param;
    int param_len;
    struct list_node list;
};

/**
 * struct brcmf_bus_ops - bus callback operations.
 *
 * @preinit: execute bus/device specific dongle init commands (optional).
 * @init: prepare for communication with dongle.
 * @stop: clear pending frames, disable data flow.
 * @txdata: send a data frame to the dongle. When the data
 *  has been transferred, the common driver must be
 *  notified using brcmf_txcomplete(). The common
 *  driver calls this function with interrupts
 *  disabled.
 * @txctl: transmit a control request message to dongle.
 * @rxctl: receive a control response message from dongle.
 * @gettxq: obtain a reference of bus transmit queue (optional).
 * @wowl_config: specify if dongle is configured for wowl when going to suspend
 * @get_ramsize: obtain size of device memory.
 * @get_memdump: obtain device memory dump in provided buffer.
 * @get_fwname: obtain firmware name.
 * @get_bootloader_macaddr: obtain mac address from bootloader, if supported.
 *
 * This structure provides an abstract interface towards the
 * bus specific driver. For control messages to common driver
 * will assure there is only one active transaction. Unless
 * indicated otherwise these callbacks are mandatory.
 */

#include "device.h"

struct brcmf_bus_ops {
    zx_status_t (*preinit)(struct brcmf_device* dev);
    void (*stop)(struct brcmf_device* dev);
    zx_status_t (*txdata)(struct brcmf_device* dev, struct brcmf_netbuf* netbuf);
    zx_status_t (*txctl)(struct brcmf_device* dev, unsigned char* msg, uint len);
    zx_status_t (*rxctl)(struct brcmf_device* dev, unsigned char* msg, uint len, int* rxlen_out);
    struct pktq* (*gettxq)(struct brcmf_device* dev);
    void (*wowl_config)(struct brcmf_device* dev, bool enabled);
    size_t (*get_ramsize)(struct brcmf_device* dev);
    zx_status_t (*get_memdump)(struct brcmf_device* dev, void* data, size_t len);
    zx_status_t (*get_fwname)(struct brcmf_device* dev, uint chip, uint chiprev,
                              unsigned char* fw_name);
    zx_status_t (*get_bootloader_macaddr)(struct brcmf_device* dev, uint8_t* mac_addr);
};

/**
 * struct brcmf_bus_msgbuf - bus ringbuf if in case of msgbuf.
 *
 * @commonrings: commonrings which are always there.
 * @flowrings: commonrings which are dynamically created and destroyed for data.
 * @rx_dataoffset: if set then all rx data has this this offset.
 * @max_rxbufpost: maximum number of buffers to post for rx.
 * @max_flowrings: maximum number of tx flow rings supported.
 * @max_submissionrings: maximum number of submission rings(h2d) supported.
 * @max_completionrings: maximum number of completion rings(d2h) supported.
 */
struct brcmf_bus_msgbuf {
    struct brcmf_commonring* commonrings[BRCMF_NROF_COMMON_MSGRINGS];
    struct brcmf_commonring** flowrings;
    uint32_t rx_dataoffset;
    uint32_t max_rxbufpost;
    uint16_t max_flowrings;
    uint16_t max_submissionrings;
    uint16_t max_completionrings;
};

/**
 * struct brcmf_bus_stats - bus statistic counters.
 *
 * @pktcowed: packets cowed for extra headroom/unorphan.
 * @pktcow_failed: packets dropped due to failed cow-ing.
 */
struct brcmf_bus_stats {
    atomic_int pktcowed;
    atomic_int pktcow_failed;
};

/**
 * struct brcmf_bus - interface structure between common and bus layer
 *
 * @bus_priv: pointer to private bus device.
 * @proto_type: protocol type, bcdc or msgbuf
 * @dev: device pointer of bus device.
 * @drvr: public driver information.
 * @state: operational state of the bus interface.
 * @stats: statistics shared between common and bus layer.
 * @maxctl: maximum size for rxctl request message.
 * @chip: device identifier of the dongle chip.
 * @always_use_fws_queue: bus wants use queue also when fwsignal is inactive.
 * @wowl_supported: is wowl supported by bus driver.
 * @chiprev: revision of the dongle chip.
 */
struct brcmf_bus {
    union {
        struct brcmf_sdio_dev* sdio;
        struct brcmf_usbdev* usb;
        struct brcmf_pciedev* pcie;
    } bus_priv;
    enum brcmf_bus_protocol_type proto_type;
    struct brcmf_device* dev;
    struct brcmf_pub* drvr;
    enum brcmf_bus_state state;
    struct brcmf_bus_stats stats;
    uint maxctl;
    uint32_t chip;
    uint32_t chiprev;
    bool always_use_fws_queue;
    bool wowl_supported;

    const struct brcmf_bus_ops* ops;
    struct brcmf_bus_msgbuf* msgbuf;
};

/*
 * callback wrappers
 */
static inline zx_status_t brcmf_bus_preinit(struct brcmf_bus* bus) {
    if (!bus->ops->preinit) {
        return ZX_OK;
    }
    return bus->ops->preinit(bus->dev);
}

static inline void brcmf_bus_stop(struct brcmf_bus* bus) {
    bus->ops->stop(bus->dev);
}

static inline int brcmf_bus_txdata(struct brcmf_bus* bus, struct brcmf_netbuf* netbuf) {
    return bus->ops->txdata(bus->dev, netbuf);
}

static inline int brcmf_bus_txctl(struct brcmf_bus* bus, unsigned char* msg, uint len) {
    return bus->ops->txctl(bus->dev, msg, len);
}

static inline int brcmf_bus_rxctl(struct brcmf_bus* bus, unsigned char* msg, uint len,
                                  int* rxlen_out) {
    return bus->ops->rxctl(bus->dev, msg, len, rxlen_out);
}

static inline zx_status_t brcmf_bus_gettxq(struct brcmf_bus* bus, struct pktq** txq_out) {
    if (!bus->ops->gettxq) {
        if (txq_out) {
            *txq_out = NULL;
        }
        return ZX_ERR_NOT_FOUND;
    }
    if (txq_out) {
        *txq_out = bus->ops->gettxq(bus->dev);
    }
    return ZX_OK;
}

static inline void brcmf_bus_wowl_config(struct brcmf_bus* bus, bool enabled) {
    if (bus->ops->wowl_config) {
        bus->ops->wowl_config(bus->dev, enabled);
    }
}

static inline size_t brcmf_bus_get_ramsize(struct brcmf_bus* bus) {
    if (!bus->ops->get_ramsize) {
        return 0;
    }

    return bus->ops->get_ramsize(bus->dev);
}

static inline zx_status_t brcmf_bus_get_memdump(struct brcmf_bus* bus, void* data, size_t len) {
    if (!bus->ops->get_memdump) {
        return ZX_ERR_NOT_FOUND;
    }

    return bus->ops->get_memdump(bus->dev, data, len);
}

static inline zx_status_t brcmf_bus_get_fwname(struct brcmf_bus* bus, uint chip, uint chiprev,
                                               unsigned char* fw_name) {
    return bus->ops->get_fwname(bus->dev, chip, chiprev, fw_name);
}

static inline zx_status_t brcmf_bus_get_bootloader_macaddr(struct brcmf_bus* bus,
                                                           uint8_t* mac_addr) {
    return bus->ops->get_bootloader_macaddr(bus->dev, mac_addr);
}

/*
 * interface functions from common layer
 */

/* Receive frame for delivery to OS.  Callee disposes of rxp. */
void brcmf_rx_frame(struct brcmf_device* dev, struct brcmf_netbuf* rxp, bool handle_event);
/* Receive async event packet from firmware. Callee disposes of rxp. */
void brcmf_rx_event(struct brcmf_device* dev, struct brcmf_netbuf* rxp);

/* Indication from bus module regarding presence/insertion of dongle. */
zx_status_t brcmf_attach(struct brcmf_device* dev, struct brcmf_mp_device* settings);
/* Indication from bus module regarding removal/absence of dongle */
void brcmf_detach(struct brcmf_device* dev);
/* Indication from bus module that dongle should be reset */
void brcmf_dev_reset(struct brcmf_device* dev);

/* Configure the "global" bus state used by upper layers */
void brcmf_bus_change_state(struct brcmf_bus* bus, enum brcmf_bus_state state);

zx_status_t brcmf_bus_started(struct brcmf_device* dev);
zx_status_t brcmf_iovar_data_set(struct brcmf_device* dev, const char* name, void* data,
                                 uint32_t len);
void brcmf_bus_add_txhdrlen(struct brcmf_device* dev, uint len);

#if CONFIG_BRCMFMAC_SDIO
void brcmf_sdio_exit(void);
zx_status_t brcmf_sdio_register(zx_device_t* zxdev, composite_protocol_t* composite_proto);
#endif
#if CONFIG_BRCMFMAC_USB
void brcmf_usb_exit(void);
zx_status_t brcmf_usb_register(zx_device_t* device, usb_protocol_t* usb_proto);
#endif
#if CONFIG_BRCMFMAC_SIM
void brcmf_sim_exit(void);
zx_status_t brcmf_sim_register(zx_device_t* device);
#endif

#endif /* BRCMFMAC_BUS_H */
