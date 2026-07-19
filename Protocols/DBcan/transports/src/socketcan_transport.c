#include "transport.h"
#include "transport_port.h" // IWYU pragma: keep
#include <libsocketcan.h>
#include <linux/can/netlink.h>
#include <net/if.h>
#include <linux/can.h>
#include <stdio.h>
#include <sys/socket.h>
#include <linux/can/raw.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

typedef struct {
    transport_t             *self;
    transport_ctx_t         base;
    transport_config_t      config;
    char                    iface[IFNAMSIZ];
    int                     socket;
    uint16_t                filter_slots[CAN_RAW_FILTER_MAX];   /* physical can_filter slot count per logical index; 0 = unused */
} socketcan_transport_t;

static const transport_ops_t socketcan_ops;
static transport_error_t can_clear_filters(transport_t *transport);

/**
  ==============================================================================
                                  ##### helpers #####
  ==============================================================================
  */

static transport_error_t errno_translator(int retval) {
    if (retval >= 0) return TP_OK;
    switch (errno) {
        case EPERM:         return TP_HW_FAULT; break;
        case ENODEV:        return TP_INVALID_ARG; break;
        case EAFNOSUPPORT:  return TP_HW_FAULT; break;
        case ENOBUFS:       return TP_TX_OVERRUN; break;
        case EINVAL:        return TP_INVALID_ARG; break;
        case ENOMEM:        return TP_HW_FAULT; break;
        default:            return TP_HW_FAULT;
    }

}

#define VENDOR_TRANSLATE(x) errno_translator(x)

static uint32_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);

    return ms;
}

static transport_error_t get_counters(socketcan_transport_t *p) {
    struct can_berr_counter counters;
    can_get_berr_counter(p->iface, &counters);

    p->base.tec     = counters.txerr;
    p->base.rec     = counters.rxerr;

    int bus_state; 
    can_get_state(p->iface, &bus_state);
    switch (bus_state) {
        case CAN_STATE_ERROR_ACTIVE:        p->base.bus_state = TP_BUS_ACTIVE;    break;
        case CAN_STATE_ERROR_WARNING:       p->base.bus_state = TP_BUS_WARNING;   break;
        case CAN_STATE_ERROR_PASSIVE:       p->base.bus_state = TP_BUS_PASSIVE;   break;
        case CAN_STATE_BUS_OFF:             p->base.bus_state = TP_BUS_OFF_STATE; break;
        case CAN_STATE_STOPPED:             p->base.bus_state = TP_BUS_OFF_STATE; break;
        case CAN_STATE_SLEEPING:            p->base.bus_state = TP_BUS_OFF_STATE; break;
        default:                            p->base.bus_state = TP_BUS_OFF_STATE; break;
    }

    return TP_OK;
}

static void record_error(socketcan_transport_t *p, transport_error_t err, const char *function, uint32_t line) {
    uint32_t saved_errno = (uint32_t)errno;
    (void)get_counters(p);

    p->base.error_count[err]++;
    p->base.last_error.timestamp_ms     = get_time_ms();
    p->base.last_error.vendor_raw       = saved_errno;
    p->base.last_error.frame_id         = 0;
    p->base.last_error.function         = function;
    p->base.last_error.line             = line;
    p->base.last_error.error_code       = err;
    p->base.last_error.bus_lec          = p->base.last_lec;
    p->base.last_error.bus_dlec         = p->base.last_dlec;
    p->base.last_error.tec              = p->base.tec;
    p->base.last_error.rec              = p->base.rec;
}

/**
  ==============================================================================
                                  ##### status #####
  ==============================================================================
  */

static const transport_ctx_t *can_get_ctx(const transport_t *transport) {
    if (!transport || !transport->ctx) return NULL;
    return &((socketcan_transport_t *)transport->ctx)->base;
}

/**
  ==============================================================================
                                  ##### init / deinit #####
  ==============================================================================
  */


transport_error_t init_transport(transport_t *transport, const char *iface) {
    if(!transport) return TP_INVALID_ARG;

    socketcan_transport_t *p = calloc(1, sizeof(socketcan_transport_t));
    if(!p) return TP_HW_FAULT;
    p->self     = transport;
    p->socket   = -1;
    strncpy(p->iface, iface, IFNAMSIZ - 1);

    transport->ctx      = p;
    transport->bus_id   = (uint8_t)if_nametoindex(iface);
    transport->ops      = &socketcan_ops;

    return TP_OK;
}


transport_error_t can_init(transport_t *transport, const transport_config_t *cfg) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;
    if (cfg->rx_int_active) return TP_INVALID_ARG;

    p->config = *cfg;
    p->base.bus_state = TP_BUS_OFF_STATE;

    struct can_ctrlmode mode = {0};

    if(cfg->fd_enabled) {
        mode.mask  |= CAN_CTRLMODE_FD;
        mode.flags |= CAN_CTRLMODE_FD;
    }
    if (cfg->mode == TP_LISTEN_MODE) {
        mode.mask  |= CAN_CTRLMODE_LISTENONLY;
        mode.flags |= CAN_CTRLMODE_LISTENONLY;
    }
    if (cfg->mode == TP_EXT_LOOPBACK_MODE || cfg->mode == TP_INT_LOOPBACK_MODE) {
        mode.mask  |= CAN_CTRLMODE_LOOPBACK;
        mode.flags |= CAN_CTRLMODE_LOOPBACK;
    }
    if (!cfg->auto_retx_enabled) {
      mode.mask  |= CAN_CTRLMODE_ONE_SHOT;
      mode.flags |= CAN_CTRLMODE_ONE_SHOT;
    }

    if (cfg->fd_enabled) {
        TRY_HAL(can_set_canfd_bitrates_samplepoint(p->iface,
            cfg->nominal_bitrate, cfg->nominal_sample_point ?: 80,
            cfg->data_bitrate,    cfg->data_sample_point    ?: 80));
    } else {
        TRY_HAL(can_set_bitrate_samplepoint(p->iface,
            cfg->nominal_bitrate, cfg->nominal_sample_point ?: 80));
    }

    TRY_HAL(can_set_ctrlmode(p->iface, &mode));

    p->socket   = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if(p->socket < 0) return errno_translator(p->socket);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = if_nametoindex(p->iface),
    };

    TRY_HAL(bind(p->socket, (struct sockaddr *)&addr, sizeof(addr)));
    TRY(can_clear_filters(transport));

    if(cfg->fd_enabled) {
        int enable = 1;
        TRY_HAL(setsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)));
    }

    TRY_HAL(can_set_restart_ms(p->iface, cfg->auto_bus_recovery_enabled ? 100 : 0));

    return TP_OK;
}

transport_error_t can_deinit(transport_t *transport) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;

    p->base.bus_state           = TP_BUS_OFF_STATE;
    p->base.bus_state_since_ms  = get_time_ms();

    if(p->socket >= 0) close(p->socket);
    p->socket                   = -1;
    memset(&p->config, 0, sizeof(p->config));

    return TP_OK;
}

/**
  ==============================================================================
                                  ##### start / stop #####
  ==============================================================================
  */

transport_error_t can_start(transport_t *transport) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;
    TRY_HAL(can_do_start(p->iface));

    p->base.bus_state = TP_BUS_ACTIVE;
    p->base.bus_state_since_ms = get_time_ms();

    return TP_OK;
}

transport_error_t can_stop(transport_t *transport) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;
    TRY_HAL(can_do_stop(p->iface));

    p->base.bus_state = TP_BUS_OFF_STATE;
    p->base.bus_state_since_ms = get_time_ms();

    return TP_OK;
}

/**
  ==============================================================================
                                  ##### filters #####
  ==============================================================================
  */

static transport_error_t can_add_filter(transport_t *transport, const transport_filter_t *filter) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;

    uint32_t width;
    switch (filter->mode) {
        case SINGLE_ID: width = 1; break;
        case DUAL_ID:   width = 2; break;
        case RANGE_ID:
            if (filter->id > filter->mask) return TP_INVALID_ARG;
            width = (filter->mask - filter->id) + 1;
            break;
        default: return TP_INVALID_ARG;
    }

    uint16_t logical_count = 0;
    while (logical_count < CAN_RAW_FILTER_MAX && p->filter_slots[logical_count] != 0) logical_count++;
    if (filter->index != logical_count) return TP_INVALID_ARG;

    socklen_t offset = 0;
    for (uint8_t i = 0; i < filter->index; i++) offset += p->filter_slots[i];

    struct can_filter existing_filters[CAN_RAW_FILTER_MAX];
    socklen_t len_existing = sizeof(existing_filters);
    TRY_HAL(getsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FILTER, existing_filters, &len_existing));
    socklen_t len = len_existing/sizeof(struct can_filter);
    if (len != offset) return TP_INVALID_ARG;   /* kernel-side list out of sync with our bookkeeping */
    if (len + width > CAN_RAW_FILTER_MAX) return TP_OVERFLOW;

    if (filter->mode == SINGLE_ID) {
        existing_filters[offset].can_id   = filter->id;
        existing_filters[offset].can_mask = filter->mask;
    } else {
        canid_t exact_mask = filter->is_ext ? (CAN_EFF_FLAG | CAN_EFF_MASK) : (CAN_EFF_FLAG | CAN_SFF_MASK);
        if (filter->mode == DUAL_ID) {
            existing_filters[offset].can_id       = filter->id;
            existing_filters[offset].can_mask     = exact_mask;
            existing_filters[offset + 1].can_id   = filter->mask;   /* second ID lives in .mask for DUAL_ID */
            existing_filters[offset + 1].can_mask = exact_mask;
        } else {
            /* RANGE_ID: one exact-match entry per ID in [id, mask] */
            for (uint32_t i = 0; i < width; i++) {
                existing_filters[offset + i].can_id   = filter->id + i;
                existing_filters[offset + i].can_mask = exact_mask;
            }
        }
    }

    TRY_HAL(setsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FILTER, existing_filters, (len + width)*sizeof(struct can_filter)));

    p->filter_slots[filter->index] = width;

    return TP_OK;
}

static transport_error_t can_remove_filter(transport_t *transport, uint8_t index, bool is_ext) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;

    uint16_t logical_count = 0;
    while (logical_count < CAN_RAW_FILTER_MAX && p->filter_slots[logical_count] != 0) logical_count++;
    if (index >= logical_count) return TP_INVALID_ARG;

    socklen_t offset = 0;
    for (uint8_t i = 0; i < index; i++) offset += p->filter_slots[i];
    uint16_t width = p->filter_slots[index];

    struct can_filter existing_filters[CAN_RAW_FILTER_MAX];
    socklen_t len_existing = sizeof(existing_filters);
    TRY_HAL(getsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FILTER, existing_filters, &len_existing));
    socklen_t len = len_existing/sizeof(struct can_filter);
    if (len < offset + width) return TP_INVALID_ARG;   /* kernel-side list out of sync with our bookkeeping */

    memmove(&existing_filters[offset], &existing_filters[offset + width], (len - offset - width)*sizeof(struct can_filter));
    len -= width;

    TRY_HAL(setsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FILTER, existing_filters, len*sizeof(struct can_filter)));

    memmove(&p->filter_slots[index], &p->filter_slots[index + 1], (logical_count - index - 1)*sizeof(p->filter_slots[0]));
    p->filter_slots[logical_count - 1] = 0;

    return TP_OK;
}

static transport_error_t can_clear_filters(transport_t *transport) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;
    TRY_HAL(setsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0));
    memset(p->filter_slots, 0, sizeof(p->filter_slots));

    return TP_OK;
}


/**
  ==============================================================================
                                  ##### send and receive #####
  ==============================================================================
  */

static transport_error_t can_send(transport_t *transport, can_frame_t *msg) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;

    if (!msg->flags.bits.fd) {
          if (msg->len > CAN_MAX_DLEN) return TP_INVALID_ARG;
          if (msg->flags.bits.brs) return TP_INVALID_ARG;    
      } else {
          if (msg->len > CANFD_MAX_DLEN) return TP_INVALID_ARG;  
      }

      struct canfd_frame frame = {0};                      
      frame.can_id = msg->id | (msg->flags.bits.ext ? CAN_EFF_FLAG : 0);
      frame.len    = msg->len;
      frame.flags  = (msg->flags.bits.fd && msg->flags.bits.brs) ? CANFD_BRS : 0;
      memcpy(frame.data, msg->data, msg->len);

      size_t  wire_len = msg->flags.bits.fd ? CANFD_MTU : CAN_MTU;
      ssize_t n = write(p->socket, &frame, wire_len);

      return (n == (ssize_t)wire_len) ? TP_OK : errno_translator((int)n);
            
}

static transport_error_t can_receive(transport_t *transport, can_frame_t *msg, uint8_t fifo) {
    CTX_OR_RETURN(transport);
    socketcan_transport_t *p = transport->ctx;

    struct canfd_frame frame;
    ssize_t bytes = read(p->socket, &frame, sizeof(frame));
    if (bytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return TP_EMPTY;
        return errno_translator((int)bytes);
    }
    if (bytes != CAN_MTU && bytes != CANFD_MTU) return TP_INVALID_ARG;

    if (frame.can_id & CAN_ERR_FLAG) {
      record_error(p, TP_BUS_ERR, __func__, __LINE__);
      return TP_BUS_ERR;
  }

    bool is_ext = (frame.can_id & CAN_EFF_FLAG) != 0;

    memset(msg, 0, sizeof(*msg));
    msg->id                 = frame.can_id & (is_ext ? CAN_EFF_MASK : CAN_SFF_MASK);
    msg->flags.bits.ext     = is_ext;
    msg->flags.bits.fd      = (bytes == CANFD_MTU);
    msg->flags.bits.brs     = (bytes == CANFD_MTU) && (frame.flags & CANFD_BRS);
    msg->len                = frame.len;
    memcpy(msg->data, frame.data, frame.len);

    return TP_OK;
}

/**
  ==============================================================================
                                ##### callbacks #####
  ==============================================================================*/

/*
 * TODO: Impliment callbacks
*/

static transport_error_t can_set_tx_cb(transport_t *transport, tx_cb_t cb) { return TP_INVALID_ARG; }
static transport_error_t can_set_rx_cb(transport_t *transport, uint8_t fifo, rx_cb_t cb) { return TP_INVALID_ARG; }
static transport_error_t can_set_fifo_event_cb(transport_t *transport, uint8_t fifo, fifo_event_cb_t event) { return TP_INVALID_ARG; }
static transport_error_t can_set_bus_event_cb(transport_t *transport, bus_event_cb_t event) {return TP_INVALID_ARG; }



static const transport_ops_t socketcan_ops = {
    .get_ctx                = can_get_ctx,
    .init                   = can_init,
    .deinit                 = can_deinit,
    .start                  = can_start,
    .stop                   = can_stop,
    .add_filter             = can_add_filter,
    .remove_filter          = can_remove_filter,
    .clear_filters          = can_clear_filters,
    .send                   = can_send,
    .receive                = can_receive,
    .set_tx_cb              = can_set_tx_cb,
    .set_rx_cb              = can_set_rx_cb,
    .set_fifo_event_cb      = can_set_fifo_event_cb,
    .set_bus_event_cb       = can_set_bus_event_cb,
};