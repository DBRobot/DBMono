#include "stm32g4xx_hal.h"
#include "transport_port.h"
#include <string.h>

#define SRAMCAN_FLS_NBR  (28U)         /* Max. Filter List Standard Number */
#define SRAMCAN_FLE_NBR  (8U)          /* Max. Filter List Extended Number */

typedef struct {
    transport_t             *self;
    transport_ctx_t         base;
    FDCAN_HandleTypeDef     *handle;
    transport_config_t      config;
    rx_cb_t                 rx_cb[2];
    fifo_event_cb_t         fifo_event_cb[2];
    bus_event_cb_t          bus_event_cb;
    volatile uint32_t       ts_counter;
    tx_cb_t                 tx_cb;
    uint8_t                 tx_marker;
    /* Slot occupancy, one bit per filter index. The peripheral has no notion
     * of a used slot -- ConfigFilter simply writes message RAM -- so the port
     * tracks it, letting a collision between two owners of the same index be
     * rejected instead of silently overwriting a live filter. */
    uint32_t                std_used;   /* SRAMCAN_FLS_NBR bits */
    uint32_t                ext_used;   /* SRAMCAN_FLE_NBR bits */
} fdcan_st_hal_transport_t;

static const transport_ops_t fdcan_st_hal_ops;

/**
  ==============================================================================
                                  ##### variables #####
  ==============================================================================
  */

static const transport_caps_t fdcan_st_hal_caps = {
    .std_filter_max = SRAMCAN_FLS_NBR,
    .ext_filter_max = SRAMCAN_FLE_NBR,
    .fifo_count     = 2,
    .rx_int_capable = true,
};

static fdcan_st_hal_transport_t ctx[3];


extern FDCAN_HandleTypeDef hfdcan1;
#if defined(FDCAN2)
extern FDCAN_HandleTypeDef hfdcan2;
#endif
#if defined(FDCAN3)
extern FDCAN_HandleTypeDef hfdcan3;
#endif

/**
  ==============================================================================
                                  ##### helpers #####
  ==============================================================================
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

#define VENDOR_TRANSLATE(x) hal_translator(x)

/* Reads hardware error counters and protocol status into p->base. */
static transport_error_t get_counters(fdcan_st_hal_transport_t *p) {
    FDCAN_ErrorCountersTypeDef  counters;                                                                                                                           
    FDCAN_ProtocolStatusTypeDef status;                                                                                                                             
                                                                                                                                                                  
    if (HAL_FDCAN_GetErrorCounters (p->handle, &counters) != HAL_OK) return TP_HW_FAULT;
    if (HAL_FDCAN_GetProtocolStatus(p->handle, &status)   != HAL_OK) return TP_HW_FAULT;

    p->base.tec = counters.TxErrorCnt;
    p->base.rec = counters.RxErrorCnt;

    if      (status.BusOff)         p->base.bus_state = BUS_STATE_BUS_OFF;
    else if (status.ErrorPassive)   p->base.bus_state = BUS_STATE_PASSIVE;
    else if (status.Warning)        p->base.bus_state = BUS_STATE_WARNING;
    else                            p->base.bus_state = BUS_STATE_ACTIVE;

    if (status.LastErrorCode     != FDCAN_PROTOCOL_ERROR_NO_CHANGE) p->base.last_lec  = status.LastErrorCode;                                                       
    if (status.DataLastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE) p->base.last_dlec = status.DataLastErrorCode;

    return TP_OK;
}

/* Maps an FDCAN HAL handle to its driver context; returns NULL if unknown. */
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

/* Snapshots TEC/REC/LEC and vendor error code into the last-error record. */
static void record_error(fdcan_st_hal_transport_t *p, transport_error_t err, const char *function, uint32_t line) {

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

/* Timestamp tick target. The counter only counts CAN bit times (1..16), so
 * resolution would otherwise track the bitrate; picking the nearest multiple
 * keeps a tick worth roughly the same time at any bus speed. */
#define TS_TARGET_NS  10000u

/* TSCC.TCP field value for the tick nearest TS_TARGET_NS at this bitrate. */
static uint32_t calc_ts_prescaler(uint32_t bitrate) {
    uint32_t bit_ns = 1000000000u / bitrate;
    uint32_t n      = (TS_TARGET_NS + bit_ns / 2u) / bit_ns;

    if (n < 1u)  n = 1u;
    if (n > 16u) n = 16u;

    return (n - 1u) << FDCAN_TSCC_TCP_Pos;
}

typedef struct {
    uint32_t max_ps;
    uint32_t max_s1;
    uint32_t max_s2;
    uint32_t min_tq;
    uint32_t max_tq;
} timing_limits_t;

static const timing_limits_t NOMINAL_LIMITS = {
    .max_ps = 512,   
    .max_s1 = 256,
    .max_s2 = 128,
    .min_tq = 8,
    .max_tq = 385,   
};

static const timing_limits_t DATA_LIMITS = {
    .max_ps = 32,
    .max_s1 = 32,
    .max_s2 = 16,
    .min_tq = 8,
    .max_tq = 49, 
};

static transport_error_t calc_timing(const timing_limits_t *lim,
                                     uint32_t clock_speed, uint32_t bitrate,
                                     uint8_t sample_point,
                                     uint32_t *prescaler, uint32_t *seg1, uint32_t *seg2)
{
    if (bitrate == 0 || clock_speed == 0)         return TP_INVALID_ARG;
    if (sample_point == 0 || sample_point >= 100) return TP_INVALID_ARG;

    uint32_t best_ps       = 0;
    uint32_t best_s1       = 0;
    uint32_t best_s2       = 0;
    uint32_t best_sp_err   = 0xFFFFFFFF;
    uint32_t best_rate_err = 0xFFFFFFFF;
    uint32_t max_rate_err = (bitrate * 3) / 200;

    for (uint32_t tq = lim->min_tq; tq <= lim->max_tq; tq++) {
        uint32_t denom = tq * bitrate;
        uint32_t ps    = (clock_speed + denom / 2) / denom;
        if (ps < 1 || ps > lim->max_ps) continue;

        uint32_t actual_rate = clock_speed / (ps * tq);
        uint32_t rate_err    = (actual_rate >= bitrate) ? (actual_rate - bitrate)
                                                        : (bitrate - actual_rate);
        if (rate_err > max_rate_err) continue;

        uint32_t s1_plus1 = (tq * sample_point + 50) / 100;
        if (s1_plus1 < 1) continue;
        uint32_t s1 = s1_plus1 - 1;
        uint32_t s2 = tq - 1 - s1;

        if (s1 < 2 || s1 > lim->max_s1) continue;
        if (s2 < 2 || s2 > lim->max_s2) continue;

        uint32_t actual_sp = ((1 + s1) * 100) / tq;
        uint32_t sp_err    = (actual_sp >= (uint32_t)sample_point)
                             ? (actual_sp - sample_point)
                             : (sample_point - actual_sp);

        if (sp_err < best_sp_err ||
            (sp_err == best_sp_err && rate_err < best_rate_err)) {
            best_sp_err   = sp_err;
            best_rate_err = rate_err;
            best_ps = ps;
            best_s1 = s1;
            best_s2 = s2;
        }
    }

    if (best_ps == 0) return TP_INVALID_ARG;

    *prescaler = best_ps;
    *seg1      = best_s1;
    *seg2      = best_s2;
    return TP_OK;
}


/**
  ==============================================================================
                                  ##### status #####
  ==============================================================================
  */

static const transport_ctx_t *canfd_get_ctx(const transport_t *transport) {
    if (!transport || !transport->ctx) return NULL;
    return &((fdcan_st_hal_transport_t *)transport->ctx)->base;
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
    transport->caps     = &fdcan_st_hal_caps;

    return TP_OK;
}

static transport_error_t canfd_init(transport_t *transport, const transport_config_t *cfg) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_InitTypeDef *init = &p->handle->Init;

    p->base.bus_state        = BUS_STATE_BUS_OFF;
    p->config                = *cfg;

    init->AutoRetransmission = cfg->auto_retx_enabled ? ENABLE : DISABLE;
    init->FrameFormat        = (cfg->fd_enabled && cfg->brs_enabled) ? FDCAN_FRAME_FD_BRS
                             : (cfg->fd_enabled) ? FDCAN_FRAME_FD_NO_BRS
                             : FDCAN_FRAME_CLASSIC;
    init->Mode               = (cfg->mode == MODE_INT_LOOPBACK) ? FDCAN_MODE_INTERNAL_LOOPBACK
                             : (cfg->mode == MODE_EXT_LOOPBACK) ? FDCAN_MODE_EXTERNAL_LOOPBACK
                             : (cfg->mode == MODE_LISTEN)       ? FDCAN_MODE_BUS_MONITORING
                             : FDCAN_MODE_NORMAL;
    init->StdFiltersNbr      = SRAMCAN_FLS_NBR;
    init->ExtFiltersNbr      = SRAMCAN_FLE_NBR;
    
    uint32_t clock_speed = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);

    if (cfg->nominal_bitrate > 0) {
        uint32_t ps, s1, s2;
        TRY(calc_timing(&NOMINAL_LIMITS, clock_speed, cfg->nominal_bitrate,
            cfg->nominal_sample_point ? cfg->nominal_sample_point : 80,
            &ps, &s1, &s2));
        init->NominalPrescaler      = ps;
        init->NominalTimeSeg1       = s1;
        init->NominalTimeSeg2       = s2;
        init->NominalSyncJumpWidth  = s2 < 4 ? s2 : 4;
    }

    if (cfg->data_bitrate > 0) {
        uint32_t ps, s1, s2;
        TRY(calc_timing(&DATA_LIMITS, clock_speed, cfg->data_bitrate,
            cfg->data_sample_point ? cfg->data_sample_point : 80,
            &ps, &s1, &s2));
        init->DataPrescaler      = ps;
        init->DataTimeSeg1       = s1;
        init->DataTimeSeg2       = s2;
        init->DataSyncJumpWidth  = s2 < 4 ? s2 : 4;
    }
    
    TRY_HAL(HAL_FDCAN_Init(p->handle));
    TRY_HAL(HAL_FDCAN_ConfigGlobalFilter(p->handle,
        FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE));

    TRY_HAL(HAL_FDCAN_ConfigTimestampCounter(p->handle,
        calc_ts_prescaler(cfg->nominal_bitrate)));
    

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
        | FDCAN_IT_RAM_ACCESS_FAILURE | FDCAN_IT_RAM_WATCHDOG | FDCAN_IT_TIMESTAMP_WRAPAROUND
        | FDCAN_IT_TX_EVT_FIFO_NEW_DATA | FDCAN_IT_TX_EVT_FIFO_ELT_LOST, 0));
    

    return TP_OK;
}


static transport_error_t canfd_deinit(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;

    p->base.bus_state   = BUS_STATE_BUS_OFF;
    p->tx_cb            = NULL;
    p->rx_cb[0]         = NULL;
    p->rx_cb[1]         = NULL;
    p->fifo_event_cb[0] = NULL;
    p->fifo_event_cb[1] = NULL;
    p->bus_event_cb     = NULL;
    memset(&p->config, 0, sizeof(p->config));

    TRY_HAL(HAL_FDCAN_DeInit(p->handle));
    
    return TP_OK;
}


/**
  ==============================================================================
                                  ##### start / stop #####
  ==============================================================================
  */

static transport_error_t canfd_start(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    TRY_HAL(HAL_FDCAN_Start(p->handle));

    p->base.bus_state          = BUS_STATE_ACTIVE;
    p->base.bus_state_since_ms = HAL_GetTick();

    return TP_OK;
}

static transport_error_t canfd_stop(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    TRY_HAL(HAL_FDCAN_Stop(p->handle));

    p->base.bus_state          = BUS_STATE_BUS_OFF;
    p->base.bus_state_since_ms = HAL_GetTick();

    return TP_OK;
}


/**
  ==============================================================================
                                  ##### filters #####
  ==============================================================================
  */

static transport_error_t canfd_add_filter(transport_t *transport, const transport_filter_t *filter) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_FilterTypeDef f;

    /* Bound against what the application actually carved out of message RAM,
     * not the RAM-layout maximum: writing past Init.*FiltersNbr lands in
     * whatever follows the filter list. */
    uint32_t max_index = filter->is_ext ? p->handle->Init.ExtFiltersNbr
                                        : p->handle->Init.StdFiltersNbr;
    if (max_index <= filter->index) return TP_INVALID_ARG;
    if (filter->fifo > 1) return TP_INVALID_ARG;

    uint32_t *used = filter->is_ext ? &p->ext_used : &p->std_used;
    if (*used & (1u << filter->index)) return TP_INVALID_ARG;   /* slot taken */

    f.IdType            = filter->is_ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    f.FilterIndex       = filter->index;
    f.FilterType        = (filter->mode == SINGLE_ID) ? FDCAN_FILTER_MASK
                        : (filter->mode == DUAL_ID) ? FDCAN_FILTER_DUAL
                        : 0;
    f.FilterConfig      = filter->fifo ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    f.FilterID1         = filter->id;
    f.FilterID2         = filter->mask;

    if(f.FilterType == 0) return TP_INVALID_ARG;

    TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));

    *used |= (1u << filter->index);

    return TP_OK;
}

static transport_error_t canfd_remove_filter(transport_t *transport, uint8_t index, bool is_ext) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_FilterTypeDef f;

    uint32_t max_index = is_ext ? p->handle->Init.ExtFiltersNbr
                                : p->handle->Init.StdFiltersNbr;
    if (max_index <= index) return TP_INVALID_ARG; 

    uint32_t *used = is_ext ? &p->ext_used : &p->std_used;
    if (!(*used & (1u << index))) return TP_INVALID_ARG;        /* slot is free */

    f.IdType        = is_ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    f.FilterIndex   = index;
    f.FilterType    = FDCAN_FILTER_MASK;
    f.FilterConfig  = FDCAN_FILTER_DISABLE;
    f.FilterID1     = 0;
    f.FilterID2     = 0;

    TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));

    *used &= ~(1u << index);

    return TP_OK;
}

static transport_error_t canfd_clear_filters(transport_t *transport) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    FDCAN_FilterTypeDef f;
    f.IdType        = FDCAN_STANDARD_ID;
    f.FilterType    = FDCAN_FILTER_MASK;
    f.FilterConfig  = FDCAN_FILTER_DISABLE;
    f.FilterID1     = 0;
    f.FilterID2     = 0;

    for(uint8_t i = 0; i < p->handle->Init.StdFiltersNbr; i++) {
        f.FilterIndex   = i;
        TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));
    }

    f.IdType        = FDCAN_EXTENDED_ID;
    for(uint8_t i = 0; i < p->handle->Init.ExtFiltersNbr; i++) {
        f.FilterIndex   = i;
        TRY_HAL(HAL_FDCAN_ConfigFilter(p->handle, &f));
    }

    p->std_used = 0;
    p->ext_used = 0;

    return TP_OK;
}


/**
  ==============================================================================
                                  ##### send and receive #####
  ==============================================================================
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
    msg->timestamp          = (p->ts_counter << 16) | header.RxTimestamp;

    p->base.rx_ok_count++;
    return TP_OK;
}

static transport_error_t canfd_send(transport_t *transport, can_frame_t *msg) {
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
    TxHeader.TxEventFifoControl     = FDCAN_STORE_TX_EVENTS;
    TxHeader.MessageMarker          = p->tx_marker;

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
    msg->tx_marker = p->tx_marker;
    p->base.tx_ok_count++;
    p->tx_marker++;

    return TP_OK;
}


/**
  ==============================================================================
                              ##### tx callbacks #####
  ==============================================================================*/

static transport_error_t canfd_set_tx_cb(transport_t *transport, tx_cb_t cb) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->tx_cb = cb;
    return TP_OK;
}

void HAL_FDCAN_TxEventFifoCallback(FDCAN_HandleTypeDef *handle, uint32_t TxEventFifoITs) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;

    if (TxEventFifoITs & FDCAN_IT_TX_EVT_FIFO_ELT_LOST) {
        record_error(p, TP_HW_FAULT, __func__, __LINE__);
    }

    if (TxEventFifoITs & FDCAN_IT_TX_EVT_FIFO_NEW_DATA) {
        FDCAN_TxEventFifoTypeDef evt;
        while (HAL_FDCAN_GetTxEvent(handle, &evt) == HAL_OK) {
            if (p->tx_cb) p->tx_cb(p->self, evt.MessageMarker, evt.TxTimestamp);
        }
    }
}


/**
  ==============================================================================
                              ##### rx callbacks #####
  ==============================================================================
  */

static transport_error_t canfd_set_rx_cb(transport_t *transport, uint8_t fifo, rx_cb_t cb) {
    CTX_OR_RETURN(transport);
    if (fifo > 1) return TP_INVALID_ARG;
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->rx_cb[fifo] = cb;
    return TP_OK;
}

static transport_error_t canfd_set_fifo_event_cb(transport_t *transport, uint8_t fifo, fifo_event_cb_t event) {
    CTX_OR_RETURN(transport);
    if (fifo > 1) return TP_INVALID_ARG;
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->fifo_event_cb[fifo] = event;

    return TP_OK;
}

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

static transport_error_t canfd_set_bus_event_cb(transport_t *transport, bus_event_cb_t event) {
    CTX_OR_RETURN(transport);
    fdcan_st_hal_transport_t *p = transport->ctx;
    p->bus_event_cb = event;

    return TP_OK;
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *handle, uint32_t interrupt) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;

    (void)interrupt;

    dbcan_bus_state_t prev_state = p->base.bus_state;
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
        p->base.bus_state          = BUS_STATE_ACTIVE;
        p->base.bus_state_since_ms = HAL_GetTick();
    }
}

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

void HAL_FDCAN_TimestampWraparoundCallback(FDCAN_HandleTypeDef *handle) {
    fdcan_st_hal_transport_t *p = lookup_ctx(handle);
    if (!p) return;
    p->ts_counter++;
}






static const transport_ops_t fdcan_st_hal_ops = {
    .get_ctx              = canfd_get_ctx,
    .init                 = canfd_init,
    .deinit               = canfd_deinit,
    .start                = canfd_start,
    .stop                 = canfd_stop,
    .add_filter           = canfd_add_filter,
    .remove_filter        = canfd_remove_filter,
    .clear_filters        = canfd_clear_filters,
    .receive              = canfd_receive,
    .send                 = canfd_send,
    .set_tx_cb            = canfd_set_tx_cb,
    .set_rx_cb            = canfd_set_rx_cb,
    .set_fifo_event_cb    = canfd_set_fifo_event_cb,
    .set_bus_event_cb     = canfd_set_bus_event_cb,
};