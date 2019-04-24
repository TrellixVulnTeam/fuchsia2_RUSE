// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <usb/usb-request.h>

#include "dwc2.h"

bool dwc_ep_write_packet(dwc_usb_t* dwc, uint8_t ep_num) {
    auto* regs = dwc->regs;
    dwc_endpoint_t* ep = &dwc->eps[ep_num];
    auto* mmio = dwc->mmio();

	uint32_t len = ep->req_length - ep->req_offset;
	if (len > ep->max_packet_size)
		len = ep->max_packet_size;

	uint32_t dwords = (len + 3) >> 2;
    uint8_t *req_buffer = &ep->req_buffer[ep->req_offset];

    auto txstatus = GNPTXSTS::Get().ReadFrom(mmio);

	while  (ep->req_offset < ep->req_length && txstatus.nptxqspcavail() > 0 && txstatus.nptxfspcavail() > dwords) {
zxlogf(LINFO, "ep_num %d nptxqspcavail %u nptxfspcavail %u dwords %u\n", ep->ep_num, txstatus.nptxqspcavail(), txstatus.nptxfspcavail(), dwords);

    	volatile uint32_t* fifo = DWC_REG_DATA_FIFO(regs, ep_num);
    
    	for (uint32_t i = 0; i < dwords; i++) {
    		uint32_t temp = *((uint32_t*)req_buffer);
//zxlogf(LINFO, "write %08x\n", temp);
    		*fifo = temp;
    		req_buffer += 4;
    	}
    
    	ep->req_offset += len;

	    len = ep->req_length - ep->req_offset;
		if (len > ep->max_packet_size)
			len = ep->max_packet_size;

	    dwords = (len + 3) >> 2;
		txstatus.ReadFrom(mmio);
	}

    if (ep->req_offset < ep->req_length) {
        // enable txempty
	    zxlogf(LINFO, "turn on nptxfempty\n");
	    GINTMSK::Get().ReadFrom(dwc->mmio()).set_nptxfempty(1).WriteTo(dwc->mmio());
		return true;
    } else {
        return false;
    }
}

void dwc_ep_start_transfer(dwc_usb_t* dwc, uint8_t ep_num, uint32_t length) {
zxlogf(LINFO, "dwc_ep_start_transfer epnum %u length %u\n", ep_num, length);
    dwc_endpoint_t* ep = &dwc->eps[ep_num];
    auto* mmio = dwc->mmio();
    bool is_in = DWC_EP_IS_IN(ep_num);

	uint32_t ep_mps = ep->max_packet_size;

    ep->req_offset = 0;
    ep->req_length = static_cast<uint32_t>(length);

    auto deptsiz = DEPTSIZ::Get(ep_num).ReadFrom(mmio);

	/* Zero Length Packet? */
    if (length == 0) {
        deptsiz.set_xfersize(is_in ? 0 : ep_mps);
        deptsiz.set_pktcnt(1);
    } else {
        deptsiz.set_pktcnt((length + (ep_mps - 1)) / ep_mps);
        if (is_in && length < ep_mps) {
            deptsiz.set_xfersize(length);
        }
        else {
            deptsiz.set_xfersize(length - ep->req_offset);
        }
    }
zxlogf(LINFO, "epnum %d is_in %d xfer_count %d xfer_len %d pktcnt %d xfersize %d\n",
        ep_num, is_in, ep->req_offset, ep->req_length, deptsiz.pktcnt(), deptsiz.xfersize());

    deptsiz.WriteTo(mmio);

	/* EP enable */
    auto depctl = DEPCTL::Get(ep_num).ReadFrom(mmio);
	depctl.set_cnak(1);
	depctl.set_epena(1);
	depctl.WriteTo(mmio);

    if (is_in) {
        dwc_ep_write_packet(dwc, ep_num);
    }
}

void dwc_complete_ep(dwc_usb_t* dwc, uint8_t ep_num) {
    zxlogf(LINFO, "XXXXX dwc_complete_ep ep_num %u\n", ep_num);

    if (ep_num != 0) {
    	dwc_endpoint_t* ep = &dwc->eps[ep_num];
        usb_request_t* req = ep->current_req;

        if (req) {
#if SINGLE_EP_IN_QUEUE
        if (DWC_EP_IS_IN(ep->ep_num)) {
            ZX_DEBUG_ASSERT(dwc->current_in_req == ep->current_req);
            dwc->current_in_req = NULL;
        }
#endif

            ep->current_req = NULL;
            dwc_usb_req_internal_t* req_int = USB_REQ_TO_INTERNAL(req);
            usb_request_complete(req, ZX_OK, ep->req_offset, &req_int->complete_cb);
        }

        ep->req_buffer = NULL;
        ep->req_offset = 0;
        ep->req_length = 0;
	}

/*
	u32 epnum = ep_num;
	if (ep_num) {
		if (!is_in)
			epnum = ep_num + 1;
	}
*/


/*
	if (is_in) {
		pcd->dwc_eps[epnum].req->actual = ep->xfer_len;
		deptsiz.d32 = dwc_read_reg32(DWC_REG_IN_EP_TSIZE(ep_num));
		if (deptsiz.b.xfersize == 0 && deptsiz.b.pktcnt == 0 &&
                    ep->xfer_count == ep->xfer_len) {
			ep->start_xfer_buff = 0;
			ep->xfer_buff = 0;
			ep->xfer_len = 0;
		}
		pcd->dwc_eps[epnum].req->status = 0;
	} else {
		deptsiz.d32 = dwc_read_reg32(DWC_REG_OUT_EP_TSIZE(ep_num));
		pcd->dwc_eps[epnum].req->actual = ep->xfer_count;
		ep->start_xfer_buff = 0;
		ep->xfer_buff = 0;
		ep->xfer_len = 0;
		pcd->dwc_eps[epnum].req->status = 0;
	}
*/
}

static void dwc_ep_queue_next_locked(dwc_usb_t* dwc, dwc_endpoint_t* ep) {
    dwc_usb_req_internal_t* req_int = NULL;

#if SINGLE_EP_IN_QUEUE
    bool is_in = DWC_EP_IS_IN(ep->ep_num);
    if (is_in) {
        if (dwc->current_in_req == NULL) {
            req_int = list_remove_head_type(&dwc->queued_in_reqs, dwc_usb_req_internal_t, node);
        }
    } else
#endif
    {
        if (ep->current_req == NULL) {
            req_int = list_remove_head_type(&ep->queued_reqs, dwc_usb_req_internal_t, node);
        }
    }
printf("dwc_ep_queue_next_locked current_req %p req_int %p\n", ep->current_req, req_int);

    if (req_int) {
        usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
        ep->current_req = req;
        
        usb_request_mmap(req, (void **)&ep->req_buffer);
        ep->send_zlp = req->header.send_zlp && (req->header.length % ep->max_packet_size) == 0;

	    dwc_ep_start_transfer(dwc, ep->ep_num, static_cast<uint32_t>(req->header.length));
    }
}

static void dwc_ep_end_transfers(dwc_usb_t* dwc, unsigned ep_num, zx_status_t reason) {
    dwc_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);

    if (ep->current_req) {
//        dwc_cmd_ep_end_transfer(dwc, ep_num);

        dwc_usb_req_internal_t* req_int = USB_REQ_TO_INTERNAL(ep->current_req);
        usb_request_complete(ep->current_req, reason, 0, &req_int->complete_cb);
        ep->current_req = NULL;
    }

    dwc_usb_req_internal_t* req_int;
    while ((req_int = list_remove_head_type(&ep->queued_reqs, dwc_usb_req_internal_t, node)) != NULL) {
        usb_request_t* req = INTERNAL_TO_USB_REQ(req_int);
        usb_request_complete(req, reason, 0, &req_int->complete_cb);
    }

    mtx_unlock(&ep->lock);
}

static void dwc_enable_ep(dwc_usb_t* dwc, unsigned ep_num, bool enable) {
    auto* mmio = dwc->mmio();

    mtx_lock(&dwc->lock);

    uint32_t bit = 1 << ep_num;

    auto mask = DAINTMSK::Get().ReadFrom(mmio).reg_value();
    if (enable) {
        auto daint = DAINT::Get().ReadFrom(mmio).reg_value();
        daint |= bit;
        mask &= ~bit;
        DAINT::Get().FromValue(daint).WriteTo(mmio);
    } else {
        mask |= bit;
    }
    DAINTMSK::Get().FromValue(mask).WriteTo(mmio);

    mtx_unlock(&dwc->lock);
}

static void dwc_ep_set_config(dwc_usb_t* dwc, unsigned ep_num, bool enable) {
    zxlogf(TRACE, "dwc3_ep_set_config %u\n", ep_num);

    if (enable) {
        dwc_enable_ep(dwc, ep_num, true);
    } else {
        dwc_enable_ep(dwc, ep_num, false);
    }
}


void dwc_reset_configuration(dwc_usb_t* dwc) {
    auto* mmio = dwc->mmio();

    mtx_lock(&dwc->lock);
    // disable all endpoints except EP0_OUT and EP0_IN
    DAINT::Get().FromValue(1).WriteTo(mmio);
    mtx_unlock(&dwc->lock);

#if SINGLE_EP_IN_QUEUE
    // Do something here
#endif

    for (uint8_t ep_num = 1; ep_num < countof(dwc->eps); ep_num++) {
        dwc_ep_end_transfers(dwc, ep_num, ZX_ERR_IO_NOT_PRESENT);
        dwc_ep_set_stall(dwc, ep_num, false);
    }
}

void dwc_start_eps(dwc_usb_t* dwc) {
    zxlogf(TRACE, "dwc3_start_eps\n");

    for (unsigned ep_num = 1; ep_num < countof(dwc->eps); ep_num++) {
        dwc_endpoint_t* ep = &dwc->eps[ep_num];
        if (ep->enabled) {
            dwc_ep_set_config(dwc, ep_num, true);

            mtx_lock(&ep->lock);
            dwc_ep_queue_next_locked(dwc, ep);
            mtx_unlock(&ep->lock);
        }
    }
}

void dwc_ep_queue(dwc_usb_t* dwc, uint8_t ep_num, usb_request_t* req) {
    dwc_endpoint_t* ep = &dwc->eps[ep_num];

    // OUT transactions must have length > 0 and multiple of max packet size
    if (DWC_EP_IS_OUT(ep_num)) {
        if (req->header.length == 0 || req->header.length % ep->max_packet_size != 0) {
            zxlogf(ERROR, "dwc_ep_queue: OUT transfers must be multiple of max packet size\n");
            dwc_usb_req_internal_t* req_int = USB_REQ_TO_INTERNAL(req);
            usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, &req_int->complete_cb);
            return;
        }
    }

    mtx_lock(&ep->lock);

    if (!ep->enabled) {
        mtx_unlock(&ep->lock);
        zxlogf(ERROR, "dwc_ep_queue ep not enabled!\n");    
        dwc_usb_req_internal_t* req_int = USB_REQ_TO_INTERNAL(req);
        usb_request_complete(req, ZX_ERR_BAD_STATE, 0, &req_int->complete_cb);
        return;
    }

    dwc_usb_req_internal_t* req_int = USB_REQ_TO_INTERNAL(req);
    list_add_tail(&ep->queued_reqs, &req_int->node);

    if (dwc->configured) {
        dwc_ep_queue_next_locked(dwc, ep);
    } else {
            zxlogf(ERROR, "dwc_ep_queue not configured!\n");    
    }

    mtx_unlock(&ep->lock);
}

zx_status_t dwc_ep_config(dwc_usb_t* dwc, const usb_endpoint_descriptor_t* ep_desc,
                          const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    auto* mmio = dwc->mmio();

    // convert address to index in range 0 - 31
    // low bit is IN/OUT
    unsigned ep_num = DWC_ADDR_TO_INDEX(ep_desc->bEndpointAddress);
zxlogf(LINFO, "dwc_ep_config address %02x ep_num %d\n", ep_desc->bEndpointAddress, ep_num);
    if (ep_num == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t ep_type = usb_ep_type(ep_desc);
    if (ep_type == USB_ENDPOINT_ISOCHRONOUS) {
        zxlogf(ERROR, "dwc_ep_config: isochronous endpoints are not supported\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    dwc_endpoint_t* ep = &dwc->eps[ep_num];

    mtx_lock(&ep->lock);

    ep->max_packet_size = usb_ep_max_packet(ep_desc);
    ep->type = ep_type;
    ep->interval = ep_desc->bInterval;
    // TODO(voydanoff) USB3 support

    ep->enabled = true;

    auto depctl = DEPCTL::Get(ep_num).ReadFrom(mmio);

    depctl.set_mps(usb_ep_max_packet(ep_desc));
	depctl.set_eptype(usb_ep_type(ep_desc));
	depctl.set_setd0pid(1);
	depctl.set_txfnum(0);   //Non-Periodic TxFIFO
	depctl.set_usbactep(1);

    depctl.WriteTo(mmio);

    dwc_enable_ep(dwc, ep_num, true);

    if (dwc->configured) {
        dwc_ep_queue_next_locked(dwc, ep);
    }

    mtx_unlock(&ep->lock);

    return ZX_OK;
}

zx_status_t dwc_ep_disable(dwc_usb_t* dwc, uint8_t ep_addr) {
    auto* mmio = dwc->mmio();

    // convert address to index in range 0 - 31
    // low bit is IN/OUT
    unsigned ep_num = DWC_ADDR_TO_INDEX(ep_addr);
    if (ep_num < 2) {
        // index 0 and 1 are for endpoint zero
        return ZX_ERR_INVALID_ARGS;
    }

    dwc_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);

    DEPCTL::Get(ep_num).ReadFrom(mmio).set_usbactep(0).WriteTo(mmio);

    ep->enabled = false;
    mtx_unlock(&ep->lock);

    return ZX_OK;
}

zx_status_t dwc_ep_set_stall(dwc_usb_t* dwc, uint8_t ep_num, bool stall) {
    if (ep_num >= countof(dwc->eps)) {
        return ZX_ERR_INVALID_ARGS;
    }

    dwc_endpoint_t* ep = &dwc->eps[ep_num];
    mtx_lock(&ep->lock);

    if (!ep->enabled) {
        mtx_unlock(&ep->lock);
        return ZX_ERR_BAD_STATE;
    }
/*
    if (stall && !ep->stalled) {
        dwc3_cmd_ep_set_stall(dwc, ep_num);
    } else if (!stall && ep->stalled) {
        dwc3_cmd_ep_clear_stall(dwc, ep_num);
    }
*/
    ep->stalled = stall;
    mtx_unlock(&ep->lock);

    return ZX_OK;
}
