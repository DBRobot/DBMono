#ifndef FDCAN_ST_HAL_TRANSPORT_H
#define FDCAN_ST_HAL_TRANSPORT_H

#include "stm32g4xx_hal.h"
#include "transport.h"

#define SRAMCAN_FLS_NBR             (28U)         /* Max. Filter List Standard Number      */
#define SRAMCAN_FLE_NBR             ( 8U)         /* Max. Filter List Extended Number      */

/**
 * @brief   definition of an FDCAN ST HAL transport instance
 */
typedef struct {
    transport_t             *self;
    transport_ctx_t         base;
    FDCAN_HandleTypeDef     *handle;
    transport_config_t      config;
    rx_cb_t                 rx_cb[2];
    fifo_event_cb_t         fifo_event_cb[2];
    bus_event_cb_t          bus_event_cb;
} fdcan_st_hal_transport_t;

/**
 * @brief   ops table for the FDCAN ST HAL transport
 */
extern const transport_ops_t fdcan_st_hal_ops;


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
