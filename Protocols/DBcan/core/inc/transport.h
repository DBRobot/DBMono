/**
  ******************************************************************************
  * @file    transport.h
  * @brief   Vendor-agnostic CAN transport abstraction layer.
  *
  *          Defines the transport_t handle, the transport_ops_t dispatch table,
  *          and the supporting types (frames, filters, configuration, error
  *          accounting, and callback signatures) used by the upper protocol
  *          layers and by every concrete transport port.
  *
  *          See README.md alongside this file for usage instructions, the
  *          contract for writing a new transport port, and the outstanding
  *          TODO list.
  ******************************************************************************
  */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct transport        transport_t;


/** ISO 11898-1 error confinement states, plus off (deliberate stop or bus-off fault). */
typedef enum {
    TP_BUS_ACTIVE,      /**< Error counters below warning threshold. */
    TP_BUS_WARNING,     /**< TEC or REC exceeded 96. */
    TP_BUS_PASSIVE,     /**< TEC or REC exceeded 127 — passive error flags only. */
    TP_BUS_OFF_STATE,   /**< TEC exceeded 255, or node deliberately stopped. */
} transport_bus_state_t;


/** Return codes for all transport operations. */
typedef enum {
    TP_OK = 0,
    TP_BUSY,
    TP_TIMEOUT,
    TP_EMPTY,
    TP_OVERFLOW,
    TP_INVALID_ARG,
    TP_NOT_INIT,
    TP_BUS_ERR,
    TP_BUS_OFF,
    TP_RX_OVERRUN,
    TP_TX_ABORT,
    TP_TX_OVERRUN,
    TP_HW_FAULT,
    TP_ERR_MAX,     /**< Sentinel — not a valid error code. */
} transport_error_t;

/** Operating mode for the CAN peripheral. */
typedef enum {
    TP_NORMAL_MODE,
    TP_INT_LOOPBACK_MODE,   /**< TX routed to RX internally; not driven to the bus pin. */
    TP_EXT_LOOPBACK_MODE,   /**< TX drives the bus pin and is also received back. */
    TP_LISTEN_MODE,         /**< Receive only — no ACKs transmitted. */
} transport_mode_t;

/** Events reported per RX FIFO. */
typedef enum {
    MESSAGE_LOST_RX,    /**< A frame was dropped because the FIFO was full. */
    RX_FULL,            /**< FIFO is at capacity — next frame will be lost. */
} fifo_event_t;

/** Bus-wide events reported via the bus event callback. */
typedef enum {
    BUS_ACTIVE,
    BUS_OFF,
    BUS_PASSIVE,
    BUS_ERROR_WARNING,
    BUS_ARBITRATION_ERROR,
    BUS_DATA_ERROR,
    RAM_ACCESS_FAILURE,
    RAM_WATCHDOG_TIMEOUT,
    BUS_TX_OVERFLOW,
} bus_event_t;

/** Frame type flags. Access bits individually via .bits or the whole byte via .raw. */
typedef union {
    uint8_t raw;
    struct {
        uint8_t ext : 1;
        uint8_t fd  : 1;
        uint8_t brs : 1;
        uint8_t reserved : 5;
    } bits;
} can_flags_t;

/** timestamp = upper-16 wraparound counter | lower-16 hardware capture. TX frames are restamped on-wire via tx_cb_t.
 *  tx_marker is written by the transport on send; match it against the marker in tx_cb_t to identify the frame. */
typedef struct {
    uint32_t    id;
    uint32_t    timestamp;
    can_flags_t flags;
    uint8_t     len;
    uint8_t     tx_marker;
    uint8_t     data[64];
} can_frame_t;


/** Snapshot of the most recent error. Accessible via get_ctx(). */
typedef struct {
    uint32_t            timestamp_ms;
    uint32_t            vendor_raw;     /**< Raw vendor error register value. */
    uint32_t            frame_id;       /**< CAN ID of the involved frame, or 0. */
    const char          *function;
    uint32_t            line;
    transport_error_t   error_code;
    uint8_t             bus_lec;        /**< Last error code, nominal phase. */
    uint8_t             bus_dlec;       /**< Last error code, data phase. */
    uint8_t             tec;
    uint8_t             rec;
} transport_last_error_t;


/** Live status snapshot. Returned read-only by get_ctx(). Counters never reset. */
typedef struct {
    // bus health
    transport_bus_state_t   bus_state;
    uint32_t                bus_state_since_ms;
    
    // accumulated errors
    uint16_t    error_count[TP_ERR_MAX];

    // traffic counters
    uint32_t    rx_ok_count;
    uint32_t    tx_ok_count;

    // live counters
    uint8_t     tec;
    uint8_t     rec;
    uint8_t     last_lec;
    uint8_t     last_dlec;

    // last failure 
    transport_last_error_t   last_error;
} transport_ctx_t;

/** Configuration passed to init(). Set unused bitrates to 0; sample points to 0 for port default (80%). */
typedef struct {
    transport_mode_t        mode;

    bool    fd_enabled;
    bool    brs_enabled;
    bool    auto_retx_enabled;
    bool    auto_bus_recovery_enabled;
    bool    rx_int_active;  /**< Deliver RX frames via rx_cb_t; if false, poll with receive(). */

    uint8_t     timestamp_us;   /**< Resolution in µs. Must divide the peripheral clock evenly. */
    uint32_t    nominal_bitrate;
    uint32_t    data_bitrate;
    uint8_t     nominal_sample_point;
    uint8_t     data_sample_point;
} transport_config_t;

/** Filter matching mode. */
typedef enum {
    SINGLE_ID,  /**< id + mask bitmatch. */
    DUAL_ID,    /**< Exact match on either id or mask. */
    RANGE_ID,   /**< Match any ID in [id, mask]. */
} transport_filter_mode_t;

/** Hardware RX filter. Filters live in message RAM indexed by index. */
typedef struct {
    transport_filter_mode_t     mode;
    uint32_t    id;
    uint32_t    mask;   /**< Bitmask, second ID, or upper bound — interpretation depends on mode. */
    uint8_t     fifo;
    uint8_t     index;
    bool        is_ext;
} transport_filter_t;

/*
 * Callbacks — all invoked from ISR context.
 *
 * tx_cb_t:         TX frame hit the wire. Use marker to identify the frame, timestamp to restamp it.
 * rx_cb_t:         Frame received (rx_int_active mode only). msg is only valid for the duration of the call.
 * fifo_event_cb_t: RX FIFO overflow event.
 * bus_event_cb_t:  Bus-wide state change or hardware fault.
 */
typedef void (*tx_cb_t)(transport_t *transport, uint8_t marker, uint32_t timestamp);
typedef void (*rx_cb_t)(transport_t *transport, uint8_t fifo, can_frame_t *msg);
typedef void (*fifo_event_cb_t)(transport_t *transport, uint8_t fifo, fifo_event_t event);
typedef void (*bus_event_cb_t)(transport_t *transport, bus_event_t event);

/** Dispatch table implemented by each port. All pointers are non-NULL after successful init_transport(). */
typedef struct {
    const transport_ctx_t *(*get_ctx)(const transport_t *transport);
    transport_error_t (*init)(transport_t *transport, const transport_config_t *cfg);
    transport_error_t (*deinit)(transport_t *transport);
    transport_error_t (*start)(transport_t *transport);
    transport_error_t (*stop)(transport_t *transport);
    transport_error_t (*add_filter)(transport_t *transport, const transport_filter_t *filter);
    transport_error_t (*remove_filter)(transport_t *transport, uint8_t index, bool is_ext);
    transport_error_t (*clear_filters)(transport_t *transport);
    transport_error_t (*send)(transport_t *transport, can_frame_t *msg);
    transport_error_t (*receive)(transport_t *transport, can_frame_t *msg, uint8_t fifo); 
    transport_error_t (*set_tx_cb)(transport_t *transport, tx_cb_t cb);
    transport_error_t (*set_rx_cb)(transport_t *transport, uint8_t fifo, rx_cb_t cb);
    transport_error_t (*set_fifo_event_cb)(transport_t *transport, uint8_t fifo, fifo_event_cb_t event);
    transport_error_t (*set_bus_event_cb)(transport_t *transport, bus_event_cb_t event);
} transport_ops_t;


/** Opaque transport handle. Allocate one per CAN bus; bind it with init_transport(). */
struct transport {
    void *ctx;                      // per-instance vendor bundle; see port header
    const transport_ops_t *ops;
    uint8_t bus_id;
};

#endif