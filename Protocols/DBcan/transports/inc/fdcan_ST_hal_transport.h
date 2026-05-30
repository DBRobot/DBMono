#ifndef FDCAN_ST_HAL_TRANSPORT_H
#define FDCAN_ST_HAL_TRANSPORT_H

#include "transport.h"

/** Binds transport to FDCAN bus 0–2. Call once before init(). */
transport_error_t init_transport(transport_t *transport, uint8_t bus);

#endif
