#include "socketcan_transport.h"
#include "transport.h"
#include <linux/can.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

/**
  ==============================================================================
                                  ##### variables #####
  ==============================================================================
  */



/**
  ==============================================================================
                                  ##### init / deinit #####
  ==============================================================================
  */

/**
 * @brief   sets up a transport instance for a specific FDCAN bus
 *
 * @param   transport pointer to the transport definition to populate
 * @param   bus FDCAN bus index (0, 1, or 2)
 *
 * @retval  error code
 */
transport_error_t init_transport(transport_t *transport, uint8_t bus) {
    if(!transport) return TP_INVALID_ARG;

    socketcan_transport_t *p = calloc(1, sizeof(socketcan_transport_t));
    p->self = transport;

    transport->ctx      = p;
    transport->bus_id   = bus;
    transport->ops      = &socketcan_ops;

    return TP_OK;
}


/**
 * @brief   initiates a canfd peripheral
 * 
 * @param   ctx pointer to the transport definition
 * @param   cfg pointer to a transport_config_t that contains the information to set up a canfd peripheral
 * 
 * @retval  error code
 */

static transport_error_t can_init(transport_t *transport, const transport_config_t *cfg) {
    if(!transport) return TP_INVALID_ARG;
    socketcan_transport_t *p = transport->ctx;

    p->base.bus_state   = TP_BUS_OFF_STATE;
    p->config           = *cfg;
    p->socket           = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    struct sockaddr_can addr = {
        .can_family = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    bind(p->socket, (struct sockaddr *)&addr, sizeof(addr));

    if(cfg->fd_enabled) {
        int on = 1;
        setsockopt(p->socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof(on));
    }

}














const transport_ops_t socketcan_ops = {
    .init                 = can_init,
    .deinit               = canfd_deinit,
}; 


