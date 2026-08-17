#include "bus_manager.h"
#include "dbcan_reg_map.h"
#include "id.h"
#include "transport.h"

#define POLL_BURST 16

#define TRY_TP(expr) do {                       \
    transport_error_t _e = (expr);              \
    if (_e != TP_OK) return _e;                 \
} while (0)

/**
  ==============================================================================
                                  ##### helpers #####
  ==============================================================================
  */

static dbcan_line_t *lines[CAN_COUNT];

static dbcan_line_t *line_of(const transport_t *tp) {
    for (uint8_t i = 0; i < CAN_COUNT; i++) {
        if (lines[i] && &lines[i]->tp == tp) return lines[i];
    }
    return NULL;
}

static transport_error_t register_line(dbcan_line_t *line) {
    if (line->index >= CAN_COUNT) return TP_INVALID_ARG;
    if (lines[line->index] && lines[line->index] != line) return TP_BUSY;

    lines[line->index] = line;
    return TP_OK;
}

static void unregister_line(const dbcan_line_t *line) {
    if (line->index < CAN_COUNT && lines[line->index] == line) {
        lines[line->index] = NULL;
    }
}

static uint8_t slots_available(const dbcan_line_t *line, bool is_ext) {
    if (!line->tp.caps) return 0;
    uint8_t hw = is_ext ? line->tp.caps->ext_filter_max : line->tp.caps->std_filter_max;
    return hw < FILTER_ARRAY_MAX_SIZE ? hw : FILTER_ARRAY_MAX_SIZE;
}

static transport_error_t claim_and_build(dbcan_line_t *line, const dbcan_id_t *id, uint32_t mask, uint8_t fifo) {
    bool is_ext = (id->kind == DBCAN_ID_PTP);
    uint8_t slot = claim_filter_slot(line, is_ext);
    if (slot >= FILTER_ARRAY_MAX_SIZE) return TP_OVERFLOW;
    return build_filter(line, id, mask, fifo, slot);
}

static void register_callbacks(dbcan_line_t *line) {
    const transport_ops_t *ops = line->tp.ops;

    /* a polled port rejects all of these; not having them is not a failure */
    if (line->cfg.rx_int_active) {
        for (uint8_t fifo = 0; fifo < line->tp.caps->fifo_count; fifo++) {
            (void)ops->set_rx_cb(&line->tp, fifo, dbcan_on_rx);
            (void)ops->set_fifo_event_cb(&line->tp, fifo, dbcan_on_fifo_event);
        }
    }
    (void)ops->set_tx_cb(&line->tp, dbcan_on_tx);
    (void)ops->set_bus_event_cb(&line->tp, dbcan_on_bus_event);
}

static transport_error_t install_claimed(dbcan_line_t *line) {
    const transport_ops_t *ops = line->tp.ops;

    for (uint8_t i = 0; i < FILTER_ARRAY_MAX_SIZE; i++) {
        if (line->std_claimed & (1u << i)) {
            TRY_TP(ops->add_filter(&line->tp, &line->std_filters[i]));
        }
        if (line->ext_claimed & (1u << i)) {
            TRY_TP(ops->add_filter(&line->tp, &line->ext_filters[i]));
        }
    }
    return TP_OK;
}

/**
  ==============================================================================
                                  ##### filters #####
  ==============================================================================
  */

transport_error_t build_filter(dbcan_line_t *line, const dbcan_id_t *id, uint32_t mask, uint8_t fifo, uint8_t index) {
    if (!line || !id || !line->tp.caps) return TP_INVALID_ARG;
    if (id->kind != DBCAN_ID_PTP && id->kind != DBCAN_ID_BCAST) return TP_INVALID_ARG;
    if (fifo >= line->tp.caps->fifo_count) return TP_INVALID_ARG;

    bool is_ext = (id->kind == DBCAN_ID_PTP);
    if (index >= slots_available(line, is_ext)) return TP_INVALID_ARG;

    uint32_t *claimed = is_ext ? &line->ext_claimed : &line->std_claimed;
    if (*claimed & (1u << index)) return TP_INVALID_ARG;

    transport_filter_t *arr = is_ext ? line->ext_filters : line->std_filters;
    arr[index] = (transport_filter_t){
        .mode   = SINGLE_ID,
        .id     = pack_id(id) & mask,
        .mask   = mask,
        .fifo   = fifo,
        .index  = index,
        .is_ext = is_ext,
    };
    *claimed |= (1u << index);

    return TP_OK;
}

uint8_t claim_filter_slot(const dbcan_line_t *line, bool is_ext) {
    if (!line) return FILTER_ARRAY_MAX_SIZE;

    uint32_t claimed = is_ext ? line->ext_claimed : line->std_claimed;
    uint8_t  max     = slots_available(line, is_ext);

    for (uint8_t i = 0; i < max; i++) {
        if (!(claimed & (1u << i))) return i;
    }
    return FILTER_ARRAY_MAX_SIZE;
}

void release_filter_slot(dbcan_line_t *line, uint8_t index, bool is_ext) {
    if (line && index < FILTER_ARRAY_MAX_SIZE) {
        uint32_t *claimed = is_ext ? &line->ext_claimed : &line->std_claimed;
        *claimed &= ~(1u << index);
    }
}

transport_error_t build_node_filters(dbcan_line_t *line) {
    if (!line || !line->tp.caps) return TP_NOT_INIT;

    uint8_t uid = reg_store.id.value.uid;

    uint8_t rsp_fifo = (line->tp.caps->fifo_count > 1) ? 1 : 0;
    uint8_t ext_room = slots_available(line, true);

    dbcan_id_t ptp = { .kind = DBCAN_ID_PTP, .u.ptp_id = { .receiver_uid = uid } };

    if (ext_room >= 2) {
        ptp.u.ptp_id.role = DBCAN_ROLE_REQUEST;
        TRY_TP(claim_and_build(line, &ptp, DBCAN_29BIT_ID_ROLE_Mask | DBCAN_29BIT_ID_RUID_Mask, 0));
        ptp.u.ptp_id.role = DBCAN_ROLE_RESPONSE;
        TRY_TP(claim_and_build(line, &ptp, DBCAN_29BIT_ID_ROLE_Mask | DBCAN_29BIT_ID_RUID_Mask, rsp_fifo));
    } else if (ext_room == 1) {
        TRY_TP(claim_and_build(line, &ptp, DBCAN_29BIT_ID_RUID_Mask, 0));
    }

    if (slots_available(line, false) >= 1) {
        const dbcan_id_t bcast = { .kind = DBCAN_ID_BCAST };
        TRY_TP(claim_and_build(line, &bcast, 0, rsp_fifo));
    }

    return TP_OK;
}

transport_error_t change_filters(dbcan_line_t *line) {
    if (!line || !line->tp.ops || !line->tp.caps) return TP_NOT_INIT;

    const transport_ops_t *ops = line->tp.ops;
    transport_error_t first = TP_OK, err;

    TRY_TP(ops->stop(&line->tp));

    err = ops->clear_filters(&line->tp);
    if (err != TP_OK) first = err;

    if (first == TP_OK) {
        err = install_claimed(line);
        if (err != TP_OK) first = err;
    }

    /* always attempted: coming back up with fewer filters than asked for beats
     * returning with the line still stopped */
    err = ops->start(&line->tp);
    if (err != TP_OK && first == TP_OK) first = err;

    return first;
}

/**
  ==============================================================================
                                  ##### config #####
  ==============================================================================
  */

transport_error_t build_config(dbcan_line_t *line) {
    if (!line || !line->tp.caps) return TP_NOT_INIT;
    if (line->index >= CAN_COUNT) return TP_INVALID_ARG;

    transport_config_t *cfg = &line->cfg;
    const reg_can_settings_t *s = &reg_store.can[line->index].settings;

    switch ((dbcan_mode_t)s->mode) {
        case MODE_NORMAL:
        case MODE_INT_LOOPBACK:
        case MODE_EXT_LOOPBACK:
        case MODE_LISTEN:   cfg->mode = (dbcan_mode_t)s->mode;  break;
        default:            cfg->mode = MODE_LISTEN;            break;
    }

    cfg->rx_int_active = line->tp.caps->rx_int_capable;

    cfg->fd_enabled                = s->fd;
    cfg->brs_enabled               = s->brs;
    cfg->auto_retx_enabled         = s->auto_retry;
    cfg->auto_bus_recovery_enabled = s->auto_recover;
    cfg->nominal_bitrate           = s->nominal_bitrate;
    cfg->data_bitrate              = s->data_bitrate;
    cfg->nominal_sample_point      = s->nominal_sample_p;
    cfg->data_sample_point         = s->data_sample_p;

    if (cfg->nominal_bitrate == 0) return TP_INVALID_ARG;
    if (cfg->fd_enabled && cfg->data_bitrate == 0) return TP_INVALID_ARG;
    if (cfg->nominal_sample_point == 0 || cfg->nominal_sample_point >= 100) return TP_INVALID_ARG;
    if (cfg->data_bitrate && (cfg->data_sample_point == 0 || cfg->data_sample_point >= 100)) {
        return TP_INVALID_ARG;
    }

    return TP_OK;
}

/**
  ==============================================================================
                                  ##### callbacks #####
  ==============================================================================
  */

__attribute__((weak)) void dbcan_on_rx(transport_t *tp, uint8_t fifo, can_frame_t *msg) {
    (void)tp; (void)fifo; (void)msg;
}

__attribute__((weak)) void dbcan_on_tx(transport_t *tp, uint8_t marker, uint32_t timestamp) {
    (void)tp; (void)marker; (void)timestamp;
}

void dbcan_on_fifo_event(transport_t *tp, uint8_t fifo, fifo_event_t event) {
    (void)fifo;
    dbcan_line_t *line = line_of(tp);
    if (!line || event != MESSAGE_LOST_RX) return;

    reg_store.can[line->index].errors.messages_lost++;
}

void dbcan_on_bus_event(transport_t *tp, bus_event_t event) {
    dbcan_line_t *line = line_of(tp);
    if (!line) return;

    reg_can_t *c = &reg_store.can[line->index];

    switch (event) {
        case BUS_ARBITRATION_ERROR: c->errors.arbitration++;  return;
        case BUS_DATA_ERROR:        c->errors.data++;         return;
        case BUS_TX_OVERFLOW:       c->errors.tx_overflow++;  return;
        case RAM_ACCESS_FAILURE:    c->errors.ram_access++;   return;
        case RAM_WATCHDOG_TIMEOUT:  c->errors.ram_watchdog++; return;
        default: break;
    }

    switch (event) {
        case BUS_OFF:   c->errors.bus_off++;    break;      /* the edge is the count */
        case BUS_ACTIVE:
        case BUS_ERROR_WARNING:
        case BUS_PASSIVE:   break;      /* a transition; the state itself comes from ctx */
        default:            return;
    }

    const transport_ctx_t *ctx = tp->ops->get_ctx(tp);
    if (ctx) {
        c->state.bus_state            = (uint8_t)ctx->bus_state;
        c->state.tec                  = ctx->tec;
        c->state.rec                  = ctx->rec;
        c->state.last_error_code      = ctx->last_lec;
        c->state.data_last_error_code = ctx->last_dlec;
    }
}

/**
  ==============================================================================
                                  ##### line lifecycle #####
  ==============================================================================
  */

transport_error_t init_can_line(dbcan_line_t *line) {
    if (!line || !line->tp.ops || !line->tp.caps) return TP_NOT_INIT;

    const transport_ops_t *ops = line->tp.ops;
    transport_error_t err;

    line->std_claimed = 0;
    line->ext_claimed = 0;

    err = register_line(line);
    if (err != TP_OK) return err;

    if ((err = build_config(line))               != TP_OK) goto fail;
    if ((err = ops->init(&line->tp, &line->cfg)) != TP_OK) goto fail;
    register_callbacks(line);
    if ((err = build_node_filters(line))         != TP_OK) goto fail;
    if ((err = install_claimed(line))            != TP_OK) goto fail;
    if ((err = ops->start(&line->tp))            != TP_OK) goto fail;

    return TP_OK;

fail:
    line->std_claimed = 0;
    line->ext_claimed = 0;
    unregister_line(line);
    return err;
}

transport_error_t deinit_can_line(dbcan_line_t *line) {
    if (!line || !line->tp.ops || !line->tp.caps) return TP_NOT_INIT;

    const transport_ops_t *ops = line->tp.ops;
    transport_error_t first = TP_OK;

    transport_error_t err = ops->stop(&line->tp);
    if (err != TP_OK) first = err;

    /* keep tearing down after a failure, so one bad slot does not strand the
     * rest installed; the first error is what gets reported */
    for (uint8_t i = 0; i < FILTER_ARRAY_MAX_SIZE; i++) {
        if (line->std_claimed & (1u << i)) {
            err = ops->remove_filter(&line->tp, i, false);
            if (err != TP_OK && first == TP_OK) first = err;
        }
        if (line->ext_claimed & (1u << i)) {
            err = ops->remove_filter(&line->tp, i, true);
            if (err != TP_OK && first == TP_OK) first = err;
        }
    }
    line->std_claimed = 0;
    line->ext_claimed = 0;

    err = ops->deinit(&line->tp);
    if (err != TP_OK && first == TP_OK) first = err;

    unregister_line(line);

    return first;
}

transport_error_t poll_line(dbcan_line_t *line) {
    if (!line || !line->tp.ops || !line->tp.caps) return TP_NOT_INIT;

    const transport_ops_t *ops = line->tp.ops;

    const transport_ctx_t *ctx = ops->get_ctx(&line->tp);
    if (ctx) {
        reg_can_state_t *st = &reg_store.can[line->index].state;
        st->bus_state       = (uint8_t)ctx->bus_state;
        st->tec                  = ctx->tec;
        st->rec                  = ctx->rec;
        st->last_error_code      = ctx->last_lec;
        st->data_last_error_code = ctx->last_dlec;
        st->rx_count             = ctx->rx_ok_count;
        st->tx_count             = ctx->tx_ok_count;

        reg_can_errors_t *e = &reg_store.can[line->index].errors;
        e->tx_abort = ctx->error_count[TP_TX_ABORT];
        e->hw_fault = ctx->error_count[TP_HW_FAULT];
        e->timeout  = ctx->error_count[TP_TIMEOUT];
    }

    if (line->cfg.rx_int_active) return TP_OK;

    /* bounded so one busy line cannot starve the caller's other work */
    for (uint8_t fifo = 0; fifo < line->tp.caps->fifo_count; fifo++) {
        for (uint8_t n = 0; n < POLL_BURST; n++) {
            can_frame_t frame;
            transport_error_t err = ops->receive(&line->tp, &frame, fifo);
            if (err == TP_EMPTY) break;
            if (err != TP_OK)    return err;
            dbcan_on_rx(&line->tp, fifo, &frame);
        }
    }

    return TP_OK;
}
