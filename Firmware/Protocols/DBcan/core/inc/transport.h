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

/**
 * @brief   attempts to exicute a function and records + returns code on fail
 * 
 * @param   expr a function that the user is attempting to call
 * 
 * @retval  transport error code 
 */
#define TRY(expr) do {                              \
    transport_error_t err = (expr);                 \
    if(err != TP_OK) {                              \
        record_error(p, err, __func__, __LINE__);   \
        return err;                                 \
    }                                               \
} while (0)


/**
 * @brief   transport state definition
 */
typedef enum {
    TP_BUS_ACTIVE,
    TP_BUS_WARNING,
    TP_BUS_PASSIVE,
    TP_BUS_OFF_STATE,
} transport_bus_state_t;


/**
 * @brief   transport error code definition
 */
typedef enum {
    TP_OK = 0,
    TP_BUSY,
    TP_TIMEOUT,
    TP_EMPTY,
    TP_INVALID_ARG,
    TP_NOT_INIT,
    TP_BUS_ERR,
    TP_BUS_OFF,
    TP_RX_OVERRUN,
    TP_TX_ABORT,
    TP_HW_FAULT,
    TP_ERR_MAX,
} transport_error_t;

/**
 * @brief   transport can bus mode definition
 */
typedef enum {
    TP_NORMAL_MODE,
    TP_INT_LOOPBACK_MODE,
    TP_EXT_LOOPBACK_MODE,
} transport_mode_t;

/**
 * @brief   definition of transport fifo interupt types
 */
typedef enum {
    MESSAGE_LOST_RX,
    RX_FULL
} fifo_event_t;

/**
 * @brief   definition of transport bus interupt types
 */
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

/**
 * @brief   definition of the bit map in a can_frame_t flags struct
 */
typedef union {
    uint8_t raw;
    struct {
        uint8_t ext : 1;
        uint8_t fd  : 1;
        uint8_t brs : 1;
        uint8_t reserved : 5;
    } bits;
} can_flags_t;

/**
 * @brief   definition of a can frame
 */

typedef struct {
    uint32_t    id;
    can_flags_t flags;
    uint8_t     len;
    uint8_t     data[64];
} can_frame_t;


/**
 * @brief   snapshot of the last error recorded
 */
typedef struct {
    uint32_t    timestamp_ms;
    uint32_t    vendor_raw;
    uint32_t    frame_id;
    const char  *function;
    uint32_t    line;
    uint8_t     error_code;
    uint8_t     bus_lec;                                                                                                    
    uint8_t     bus_dlec;                                                                                                                                          
    uint8_t     tec;                                                                                                                                               
    uint8_t     rec;  
} transport_last_error_t;


/**
 * @brief   snapshot of the bus state
 */
typedef struct {
    // bus health
    uint8_t     bus_state;
    uint32_t    bus_state_since_ms;
    
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
    uint32_t    lec_cached_ms;

    // last failure 
    transport_last_error_t   last_error;
} transport_ctx_t;

/**
 * @brief   definition of the can configuration parameters
 */
typedef struct {
    transport_mode_t        mode;
    
    bool    fd_enabled;
    bool    brs_enabled;
    bool    auto_retx_enabled;
    bool    auto_bus_recovery_enabled;
    bool    rx_int_active;
} transport_config_t;

/**
 * @brief   definition of a transport can filter
 */
typedef struct {
    uint32_t    id;
    uint32_t    mask;
    uint8_t     fifo;
    uint8_t     index;
    bool        is_ext;
} transport_filter_t;

/**
 * @brief   definition of a transport RX frame callback
 */
typedef void (*rx_cb_t)(transport_t *transport, uint8_t fifo, can_frame_t *msg);

/**
 * @brief   definition of a transport bus event callback
 */
typedef void (*fifo_event_cb_t)(transport_t *transport, uint8_t fifo, fifo_event_t event);

/**
 * @brief   definition of a bus state change callback
 */
typedef void (*bus_event_cb_t)(transport_t *transport, bus_event_t event);

/**
 * @brief   ops table of all functions allowed by the transport
 */
typedef struct {
    transport_error_t (*init)(transport_t *transport, const transport_config_t *cfg);
    transport_error_t (*deinit)(transport_t *transport);
    transport_error_t (*start)(transport_t *transport);
    transport_error_t (*stop)(transport_t *transport);
    transport_error_t (*add_filter)(transport_t *transport, const transport_filter_t *filter);
    transport_error_t (*remove_filter)(transport_t *transport, uint8_t index, bool is_ext);
    transport_error_t (*clear_filters)(transport_t *transport);
    transport_error_t (*send)(transport_t *transport, const can_frame_t *msg);
    transport_error_t (*receive)(transport_t *transport, can_frame_t *msg, uint8_t fifo); 
    transport_error_t (*set_rx_cb)(transport_t *transport, uint8_t fifo, rx_cb_t cb);
    transport_error_t (*set_fifo_event_cb)(transport_t *transport, uint8_t fifo, fifo_event_cb_t event);
    transport_error_t (*set_bus_event_cb)(transport_t *transport, bus_event_cb_t event);
} transport_ops_t;


/**
 * @brief   definition of the transport that combines an instance to a set of functions
 */
struct transport {
    void *ctx;                      // per-instance vendor bundle; see port header
    const transport_ops_t *ops;
    uint8_t bus_id;
};

#endif