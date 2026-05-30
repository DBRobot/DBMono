#ifndef SOCKETCAN_TRANSPORT_H
#define SOCKETCAN_TRANSPORT_H

#include <linux/can.h>

#include "transport.h"

typedef struct {
    transport_t         *self;
    transport_ctx_t     base;
    int                 socket;
    transport_config_t  config;
} socketcan_transport_t;

extern const transport_ops_t socketcan_ops;

/**
 * @brief   sets up a transport instance for a specific FDCAN bus
 *
 * @param   transport pointer to the transport definition to populate
 * @param   bus FDCAN bus index (0, 1, or 2)
 *
 * @retval  error code
 */
transport_error_t init_transport(transport_t *transport, uint8_t bus);

#endif