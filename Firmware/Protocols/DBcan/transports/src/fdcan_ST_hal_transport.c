#include "fdcan_ST_hal_transport.h"


/**
  ==============================================================================
                                  ##### variables #####
  ==============================================================================
  */

static fdcan_st_hal_transport_t ctx[3];


extern FDCAN_HandleTypeDef hfdcan1;
#if defined(FDCAN2)
extern FDCAN_HandleTypeDef hfdcan2;
#endif
#if defined(FDCAN3)
extern FDCAN_HandleTypeDef hfdcan3;
#endif


static const uint8_t dlc_to_bytes[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};

static const uint8_t bytes_to_dlc[65] = {                                                                                                                       
    [0]         = FDCAN_DLC_BYTES_0,                                                                                                                            
    [1]         = FDCAN_DLC_BYTES_1,                                                                                                                            
    [2]         = FDCAN_DLC_BYTES_2,                                                                                                                          
    [3]         = FDCAN_DLC_BYTES_3,                                                                                                                            
    [4]         = FDCAN_DLC_BYTES_4,                                                                                                                            
    [5]         = FDCAN_DLC_BYTES_5,
    [6]         = FDCAN_DLC_BYTES_6,                                                                                                                            
    [7]         = FDCAN_DLC_BYTES_7,                                                                                                                            
    [8]         = FDCAN_DLC_BYTES_8,
    [9  ... 12] = FDCAN_DLC_BYTES_12,                                                                                                                           
    [13 ... 16] = FDCAN_DLC_BYTES_16,                                                                                                                         
    [17 ... 20] = FDCAN_DLC_BYTES_20,                                                                                                                           
    [21 ... 24] = FDCAN_DLC_BYTES_24,                                                                                                                           
    [25 ... 32] = FDCAN_DLC_BYTES_32,
    [33 ... 48] = FDCAN_DLC_BYTES_48,                                                                                                                           
    [49 ... 64] = FDCAN_DLC_BYTES_64,
};


/**
 * @brief   guards an op that takes a transport_t *. Returns TP_NOT_INIT
 *          if the transport or its ctx is NULL (e.g. init_transport
 *          was never called).
 */
#define CTX_OR_RETURN(t)                                                       \
    do {                                                                       \
        if (!(t) || !(t)->ctx) return TP_NOT_INIT;                             \
    } while (0)

/**
 * @brief   wraps a HAL_FDCAN_* call: translates its return into a
 *          transport_error_t, records the error at origin, and propagates.
 */
#define TRY_HAL(expr) TRY(hal_translator(expr))


/**
  ==============================================================================
                                  ##### helpers #####
  ==============================================================================
  */

/**
 * @brief   turns ST HAL errors into TP errors
 * 
 * @param   h return status that is being translated
 * 
 * @retval  TP error code
 */
static transport_error_t hal_translator(HAL_StatusTypeDef h) {
    switch(h) {
        case HAL_OK:        return TP_OK;
        case HAL_ERROR:     return TP_HW_FAULT;
        case HAL_BUSY:      return TP_BUSY;
        case HAL_TIMEOUT:   return TP_TIMEOUT;
        default:            return TP_HW_FAULT;
    }
}

/**
 * @brief   updates the bus counters for the transports bus handle
 * 
 * @param   p transport getting updated
 * 
 * @retval  TP error code
 */
static transport_error_t get_counters(fdcan_st_hal_transport_t *p) {
    FDCAN_ErrorCountersTypeDef  counters;                                                                                                                           
    FDCAN_ProtocolStatusTypeDef status;                                                                                                                             
                                                                                                                                                                  
    if (HAL_FDCAN_GetErrorCounters (p->handle, &counters) != HAL_OK) return TP_HW_FAULT;
    if (HAL_FDCAN_GetProtocolStatus(p->handle, &status)   != HAL_OK) return TP_HW_FAULT;

    p->base.tec = counters.TxErrorCnt;
    p->base.rec = counters.RxErrorCnt;

    if      (status.BusOff)         p->base.bus_state = TP_BUS_OFF_STATE;
    else if (status.ErrorPassive)   p->base.bus_state = TP_BUS_PASSIVE;
    else if (status.Warning)        p->base.bus_state = TP_BUS_WARNING;
    else                            p->base.bus_state = TP_BUS_ACTIVE;

    if (status.LastErrorCode     != FDCAN_PROTOCOL_ERROR_NO_CHANGE) p->base.last_lec  = status.LastErrorCode;                                                       
    if (status.DataLastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE) p->base.last_dlec = status.DataLastErrorCode;

    return TP_OK;
}

/**
 * @brief   maps an FDCAN HAL handle back to its driver context
 *
 * @param   handle HAL FDCAN handle from an ISR
 *
 * @retval  pointer to the matching driver context, or NULL if unknown
 */
static fdcan_st_hal_transport_t *lookup_ctx(FDCAN_HandleTypeDef *handle) {
    if (handle->Instance == FDCAN1) return &ctx[0];
#if defined(FDCAN2)
    if (handle->Instance == FDCAN2) return &ctx[1];
#endif
#if defined(FDCAN3)
    if (handle->Instance == FDCAN3) return &ctx[2];
#endif
    return NULL;
}

/**
 * @brief   records the last error to the transport instace
 *
 * @param   ctx pointer to the transport definition
 * @param   err transport_error_t that describes the error that occered
 * @param   function the function where the error occered
 * @param   line the line of code where the error occered
 *
 * @retval  error code
 */
static void record_error(void *ctx, transport_error_t err, const char *function, uint32_t line) {
    fdcan_st_hal_transport_t *p = ctx;

    (void)get_counters(p);
    p->base.error_count[err]++;
    p->base.last_error.timestamp_ms     = HAL_GetTick();
    p->base.last_error.vendor_raw       = p->handle->ErrorCode;
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
    if (!transport) return TP_INVALID_ARG;
    if (bus >= sizeof(ctx) / sizeof(ctx[0])) return TP_INVALID_ARG;

    FDCAN_HandleTypeDef *handle = (bus == 0) ? &hfdcan1
#if defined(FDCAN2)
                                : (bus == 1) ? &hfdcan2
#endif
#if defined(FDCAN3)
                                : (bus == 2) ? &hfdcan3
#endif
                                : NULL;
    if (!handle) return TP_INVALID_ARG;

    fdcan_st_hal_transport_t *p = &ctx[bus];
    memset(p, 0, sizeof(*p));
    p->handle           = handle;
    p->self             = transport;
    
    transport->ctx      = p;
    transport->bus_id   = bus;
    transport->ops      = &fdcan_st_hal_ops;

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

static transport_error_t canfd_init(transport_t *transport, const transport_config_t *cfg) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_InitTypeDef *init = &p->handle->Init;

    p->base.bus_state        = TP_BUS_OFF_STATE;     /* peripheral configured but not yet started */
    p->config                = *cfg;

    init->AutoRetransmission = cfg->auto_retx_enabled ? ENABLE : DISABLE;
    init->FrameFormat        = (cfg->fd_enabled && cfg->brs_enabled) ? FDCAN_FRAME_FD_BRS
                             : (cfg->fd_enabled) ? FDCAN_FRAME_FD_NO_BRS
                             : FDCAN_FRAME_CLASSIC;
    init->Mode               = (cfg->mode == TP_INT_LOOPBACK_MODE) ? FDCAN_MODE_INTERNAL_LOOPBACK
                             : (cfg->mode == TP_EXT_LOOPBACK_MODE) ? FDCAN_MODE_EXTERNAL_LOOPBACK
                             : FDCAN_MODE_NORMAL;
    init->StdFiltersNbr      = SRAMCAN_FLS_NBR;
    init->ExtFiltersNbr      = SRAMCAN_FLE_NBR;
    
    TRY_HAL(HAL_FDCAN_Init(p->handle));
    TRY_HAL(HAL_FDCAN_ConfigGlobalFilter(p->handle,
        FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE));

    if (cfg->rx_int_active) {
        TRY_HAL(HAL_FDCAN_ActivateNotification(p->handle,
            FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_RX_FIFO0_FULL, 0));
        TRY_HAL(HAL_FDCAN_ActivateNotification(p->handle,
            FDCAN_IT_RX_FIFO1_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_MESSAGE_LOST | FDCAN_IT_RX_FIFO1_FULL, 0));
    }

    /* bus state and protocol-error notifications are always enabled —
       they apply regardless of whether RX is interrupt-driven or polled */
    TRY_HAL(HAL_FDCAN_ActivateNotification(p->handle,
        FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING, 0));
    TRY_HAL(HAL_FDCAN_ActivateNotification(p->handle,
        FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR
        | FDCAN_IT_RAM_ACCESS_FAILURE | FDCAN_IT_RAM_WATCHDOG, 0));
    

    return TP_OK;
}


/**
 * @brief   uninitializes a canfd peripheral
 * 
 * @param   ctx pointer to the transport definition
 * 
 * @retval  error code
 */
static transport_error_t canfd_deinit(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;

    p->base.bus_state = TP_BUS_OFF_STATE;
    memset(&p->config, 0, sizeof(p->config));

    TRY_HAL(HAL_FDCAN_DeInit(p->handle));
    
    return TP_OK;
}


/**
  ==============================================================================
                                  ##### start / stop #####
  ==============================================================================
  */

/**
 * @brief   HAL wrapper to start a canfd peripheral
 * 
 * @param   ctx pointer to the transport definition
 * 
 * @retval  error code
 */
static transport_error_t canfd_start(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    TRY_HAL(HAL_FDCAN_Start(p->handle));

    p->base.bus_state          = TP_BUS_ACTIVE;
    p->base.bus_state_since_ms = HAL_GetTick();

    return TP_OK;
}

/**
 * @brief   HAL wrapper to stop a canfd peripheral
 * 
 * @param   ctx pointer to the transport definition
 * 
 * @retval  error code
 */
static transport_error_t canfd_stop(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    TRY_HAL(HAL_FDCAN_Stop(p->handle));

    return TP_OK;
}


/**
  ==============================================================================
                                  ##### filters #####
  ==============================================================================
  */

/**
 * @brief   adds a filter into ram 
 * 
 * @param   ctx pointer to the transport definition
 * @param   filter pointer to a transport_filter_t that contains all the information to set up a hardware filter
 * 
 * @retval  error code
 */
static transport_error_t canfd_add_filter(transport_t *transport, const transport_filter_t *filter) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_FilterTypeDef f;

    uint32_t max_index = filter->is_ext ? SRAMCAN_FLE_NBR : SRAMCAN_FLS_NBR;
    if (max_index <= filter->index) return TP_INVALID_ARG;
    if (filter->fifo > 1) return TP_INVALID_ARG;

    f.IdType            = filter->is_ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    f.FilterIndex       = filter->index;
    f.FilterType        = FDCAN_FILTER_MASK;
    f.FilterConfig      = filter->fifo ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    f.FilterID1         = filter->id;
    f.FilterID2         = filter->mask;

    TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));

    return TP_OK;
}

/**
 * @brief   removes a hardware filter from RAM
 *
 * @param   ctx pointer to the transport definition
 * @param   index uint8_t of the filter index being removed
 * @param   is_ext bool of if the filter is for extended ids
 *
 * @retval  error code
 */
static transport_error_t canfd_remove_filter(transport_t *transport, uint8_t index, bool is_ext) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_FilterTypeDef f;

    uint32_t max_index = is_ext ? SRAMCAN_FLE_NBR : SRAMCAN_FLS_NBR;
    if (max_index <= index) return TP_INVALID_ARG; 

    f.IdType        = is_ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    f.FilterIndex   = index;
    f.FilterType    = FDCAN_FILTER_MASK;
    f.FilterConfig  = FDCAN_FILTER_DISABLE;
    f.FilterID1     = 0;
    f.FilterID2     = 0;

    TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));

    return TP_OK;
}

/**
 * @brief   removes all hardware filters from RAM
 *
 * @param   ctx pointer to the transport definition
 *
 * @retval  error code
 */
static transport_error_t canfd_clear_filters(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_FilterTypeDef f;
    f.IdType        = FDCAN_STANDARD_ID;
    f.FilterType    = FDCAN_FILTER_MASK;
    f.FilterConfig  = FDCAN_FILTER_DISABLE;
    f.FilterID1     = 0;
    f.FilterID2     = 0;

    for(uint8_t i = 0; i < SRAMCAN_FLS_NBR; i++) {
        f.FilterIndex   = i;
        TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));
    }

    f.IdType        = FDCAN_EXTENDED_ID;
    for(uint8_t i = 0; i < SRAMCAN_FLE_NBR; i++) {
        f.FilterIndex   = i;
        TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));
    }


    return TP_OK;
}


/**
  ==============================================================================
                                  ##### send and receive #####
  ==============================================================================
  */


/**
 * @brief   retrieves a message directly from the rx fifo and stores it in a can_frame_t
 * 
 * @param   ctx pointer to the transport definition
 * @param   msg pointer to the can_frame_t the message is being transfered to
 * @param   fifo the fifo being read
 * 
 * @retval  error code
 */
static transport_error_t canfd_receive(transport_t *transport, can_frame_t *msg, uint8_t fifo) {
    CTX_OR_RETURN(transport);
    if (fifo > 1) return TP_INVALID_ARG;

    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_RxHeaderTypeDef header;
    uint32_t RxLocation = fifo ? FDCAN_RX_FIFO1 : FDCAN_RX_FIFO0;

    TRY_HAL(HAL_FDCAN_GetRxMessage(p->handle, RxLocation, &header, msg->data));

    msg->flags.raw          = 0;
    msg->id                 = header.Identifier;
    msg->len                = dlc_to_bytes[header.DataLength & 0xF];
    msg->flags.bits.brs     = (header.BitRateSwitch == FDCAN_BRS_ON);
    msg->flags.bits.ext     = (header.IdType == FDCAN_EXTENDED_ID);
    msg->flags.bits.fd      = (header.FDFormat == FDCAN_FD_CAN);

    p->base.rx_ok_count++;
    return TP_OK;
}

/**
 * @brief   adds a message to the hardware tx queue
 * 
 * @param   ctx pointer to the transport definition
 * @param   msg the msg being sent
 * 
 * @retval  error code
 */
static transport_error_t canfd_send(transport_t *transport, const can_frame_t *msg) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_TxHeaderTypeDef TxHeader;

    if (msg->len > 64) return TP_INVALID_ARG;
    if (HAL_FDCAN_GetTxFifoFreeLevel(p->handle) == 0) return TP_BUSY;

    TxHeader.Identifier             = msg->id;
    TxHeader.IdType                 = msg->flags.bits.ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID; 
    TxHeader.TxFrameType            = FDCAN_DATA_FRAME;
    TxHeader.DataLength             = bytes_to_dlc[msg->len];
    TxHeader.ErrorStateIndicator    = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch          = msg->flags.bits.brs ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    TxHeader.FDFormat               = msg->flags.bits.fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl     = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker          = 0;

    HAL_StatusTypeDef hs = HAL_FDCAN_AddMessageToTxFifoQ(p->handle, &TxHeader, (uint8_t *)msg->data);
    if (hs != HAL_OK) {
        /* Disambiguate FIFO-full race (FIFO drained empty between our pre-check
           and the add) from a genuine hardware fault. The HAL sets
           HAL_FDCAN_ERROR_FIFO_FULL on the handle when this is the cause. */
        transport_error_t err;
        if (p->handle->ErrorCode & HAL_FDCAN_ERROR_FIFO_FULL) {
            err = TP_BUSY;
            p->handle->ErrorCode &= ~HAL_FDCAN_ERROR_FIFO_FULL;
        } else {
            err = hal_translator(hs);
        }
        record_error(p, err, __func__, __LINE__);
        p->base.last_error.frame_id = msg->id;
        return err;
    }
    p->base.tx_ok_count++;

    return TP_OK;
}


/**
  ==============================================================================
                              ##### rx callbacks #####
  ==============================================================================
  */

/**
 * @brief   registers a callback to be invoked on received frames for a given fifo
 *
 * @param   ctx pointer to the transport definition
 * @param   fifo the rx fifo
 * @param   cb the callback function to install
 *
 * @retval  error code
 */
static transport_error_t canfd_set_rx_cb(transport_t *transport, uint8_t fifo, rx_cb_t cb) {
    CTX_OR_RETURN(transport);
    if (fifo > 1) return TP_INVALID_ARG;
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->rx_cb[fifo] = cb;
    return TP_OK;
}

/**
 * @brief   registers a callback to be invoked on fifo events for a given fifo
 *
 * @param   ctx pointer to the transport definition
 * @param   fifo the rx fifo
 * @param   event the callback function to install
 *
 * @retval  error code
 */
static transport_error_t canfd_set_fifo_event_cb(transport_t *transport, uint8_t fifo, fifo_event_cb_t event) {
    CTX_OR_RETURN(transport);
    if (fifo > 1) return TP_INVALID_ARG;
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->fifo_event_cb[fifo] = event;

    return TP_OK;
}

/**
 * @brief   HAL override invoked on RX FIFO 0 interrupts
 *
 * @param   handle HAL FDCAN handle that triggered the callback
 * @param   interrupt bitmask of interrupt sources that fired
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *handle, uint32_t interrupt) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;

    if (interrupt & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) {
        can_frame_t msg;
        if (canfd_receive(p->self, &msg, 0) == TP_OK && p->rx_cb[0]) {
            p->rx_cb[0](p->self, 0, &msg);
        }
    }

    if (interrupt & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) {
        record_error(p, TP_RX_OVERRUN, __func__, __LINE__);
    }

    if (p->fifo_event_cb[0]) {
        if (interrupt & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) p->fifo_event_cb[0](p->self, 0, MESSAGE_LOST_RX);
        if (interrupt & FDCAN_IT_RX_FIFO0_FULL)         p->fifo_event_cb[0](p->self, 0, RX_FULL);
    }
}


/**
 * @brief   HAL override invoked on RX FIFO 1 interrupts
 *
 * @param   handle HAL FDCAN handle that triggered the callback
 * @param   interrupt bitmask of interrupt sources that fired
 */
void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *handle, uint32_t interrupt) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;

    if (interrupt & FDCAN_IT_RX_FIFO1_NEW_MESSAGE) {
        can_frame_t msg;
        if (canfd_receive(p->self, &msg, 1) == TP_OK && p->rx_cb[1]) {
            p->rx_cb[1](p->self, 1, &msg);
        }
    }

    if (interrupt & FDCAN_IT_RX_FIFO1_MESSAGE_LOST) {
        record_error(p, TP_RX_OVERRUN, __func__, __LINE__);
    }

    if (p->fifo_event_cb[1]) {
        if (interrupt & FDCAN_IT_RX_FIFO1_MESSAGE_LOST) p->fifo_event_cb[1](p->self, 1, MESSAGE_LOST_RX);
        if (interrupt & FDCAN_IT_RX_FIFO1_FULL)         p->fifo_event_cb[1](p->self, 1, RX_FULL);
    }
}

/**
  ==============================================================================
                           ##### state callbacks #####
  ==============================================================================
  */

/**
 * @brief   registers a callback to be invoked on bus-wide events
 *
 * @param   ctx pointer to the transport definition
 * @param   event the callback function to install
 *
 * @retval  error code
 */
static transport_error_t canfd_set_bus_event_cb(transport_t *transport, bus_event_cb_t event) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->bus_event_cb = event;

    return TP_OK;
}

/**
 * @brief   HAL override invoked on bus state changes (warning / passive / off / recovery)
 *
 * @param   handle HAL FDCAN handle that triggered the callback
 * @param   interrupt bitmask of bus-state interrupt sources that fired
 */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *handle, uint32_t interrupt) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;

    (void)interrupt;

    uint8_t prev_state = p->base.bus_state;
    (void)get_counters(p);
    if (p->base.bus_state != prev_state) {
        p->base.bus_state_since_ms = HAL_GetTick();
    }

    uint32_t psr = handle->Instance->PSR;
    bus_event_t evt;
    if      (psr & FDCAN_PSR_BO) { evt = BUS_OFF;            record_error(p, TP_BUS_OFF, __func__, __LINE__); }
    else if (psr & FDCAN_PSR_EP) { evt = BUS_PASSIVE;        record_error(p, TP_BUS_ERR, __func__, __LINE__); }
    else if (psr & FDCAN_PSR_EW) { evt = BUS_ERROR_WARNING;  record_error(p, TP_BUS_ERR, __func__, __LINE__); }
    else                         { evt = BUS_ACTIVE;        /* recovered — no error to record */ }

    if (p->bus_event_cb) p->bus_event_cb(p->self, evt);

    if (evt == BUS_OFF && p->config.auto_bus_recovery_enabled) {
        HAL_FDCAN_Stop(p->handle);
        HAL_FDCAN_Start(p->handle);
    }
}

/**
 * @brief   HAL override invoked on protocol-level and RAM-access errors
 *
 * @param   handle HAL FDCAN handle that triggered the callback
 */
void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *handle) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;

    uint32_t err = handle->ErrorCode;
    if (err == HAL_FDCAN_ERROR_NONE) return;

    if (p->bus_event_cb) {
        if (err & HAL_FDCAN_ERROR_PROTOCOL_ARBT) p->bus_event_cb(p->self, BUS_ARBITRATION_ERROR);
        if (err & HAL_FDCAN_ERROR_PROTOCOL_DATA) p->bus_event_cb(p->self, BUS_DATA_ERROR);
        if (err & HAL_FDCAN_ERROR_RAM_ACCESS)    p->bus_event_cb(p->self, RAM_ACCESS_FAILURE);
        if (err & HAL_FDCAN_ERROR_RAM_WDG)       p->bus_event_cb(p->self, RAM_WATCHDOG_TIMEOUT);
    }

    record_error(p, TP_HW_FAULT, __func__, __LINE__);
    handle->ErrorCode = HAL_FDCAN_ERROR_NONE;
}





const transport_ops_t fdcan_st_hal_ops = {
    .init                 = canfd_init,
    .deinit               = canfd_deinit,
    .start                = canfd_start,
    .stop                 = canfd_stop,
    .add_filter           = canfd_add_filter,
    .remove_filter        = canfd_remove_filter,
    .clear_filters        = canfd_clear_filters,
    .receive              = canfd_receive,
    .send                 = canfd_send,
    .set_rx_cb            = canfd_set_rx_cb,
    .set_fifo_event_cb    = canfd_set_fifo_event_cb,
    .set_bus_event_cb     = canfd_set_bus_event_cb,
}; 