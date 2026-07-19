#ifndef TRANSPORT_PORT_H
#define TRANSPORT_PORT_H

#include "transport.h" // IWYU pragma: keep

/** Propagates errors. Requires local `p` (port context) and a static record_error() in scope. */
#define TRY(expr) do {                              \
    transport_error_t err = (expr);                 \
    if(err != TP_OK) {                              \
        record_error(p, err, __func__, __LINE__);   \
        return err;                                 \
    }                                               \
} while (0)

#define CTX_OR_RETURN(t)                                                       \
    do {                                                                       \
        if (!(t) || !(t)->ctx) return TP_NOT_INIT;                             \
    } while (0)

/** Like TRY but for vendor HAL calls. Port must define VENDOR_TRANSLATE(x) → transport_error_t. */
#define TRY_HAL(expr) TRY(VENDOR_TRANSLATE(expr)) 


/* DLC lookup tables — standard CAN FD encoding, vendor-agnostic.
 * bytes_to_dlc maps payload length (0..64) to the 4-bit DLC field value (0..15).
 * dlc_to_bytes maps the 4-bit DLC value back to payload length. */
static const uint8_t dlc_to_bytes[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};

static const uint8_t bytes_to_dlc[65] = {
    [0]         = 0,
    [1]         = 1,
    [2]         = 2,
    [3]         = 3,
    [4]         = 4,
    [5]         = 5,
    [6]         = 6,
    [7]         = 7,
    [8]         = 8,
    [9  ... 12] = 9,
    [13 ... 16] = 10,
    [17 ... 20] = 11,
    [21 ... 24] = 12,
    [25 ... 32] = 13,
    [33 ... 48] = 14,
    [49 ... 64] = 15,
};

/*
 * Port init functions — include this header and define the appropriate port macro
 * in your build system to get the right init_transport declaration.
 * The protocol layer never calls these; only platform integration code does.
 */
#ifdef TRANSPORT_PORT_ST_FDCAN
transport_error_t init_transport(transport_t *transport, uint8_t bus);
#endif

#ifdef TRANSPORT_PORT_SOCKETCAN
transport_error_t init_transport(transport_t *transport, const char *iface);
#endif

#endif
