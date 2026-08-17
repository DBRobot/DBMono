#ifndef BUS_MANAGER_H
#define BUS_MANAGER_H

#include "transport.h"
#include "id.h"

/* per format, and ours alone -- the port's own ceiling comes from transport_caps_t */
#define FILTER_ARRAY_MAX_SIZE 16

typedef struct {
    transport_t         tp;
    transport_filter_t  std_filters[FILTER_ARRAY_MAX_SIZE];
    transport_filter_t  ext_filters[FILTER_ARRAY_MAX_SIZE];
    uint32_t            std_claimed;
    uint32_t            ext_claimed;
    transport_config_t  cfg;
    uint8_t             index;          /* which reg_store.can[] line this is */
} dbcan_line_t;

/* mask selects which ID bits must match; widen it to cover more traffic in
 * fewer slots. See the DBCAN_*_Mask defines in id.h. */
transport_error_t build_filter(dbcan_line_t *line, const dbcan_id_t *id, uint32_t mask, uint8_t fifo, uint8_t index);
uint8_t claim_filter_slot(const dbcan_line_t *line, bool is_ext);
void release_filter_slot(dbcan_line_t *line, uint8_t index, bool is_ext);

transport_error_t build_config(dbcan_line_t *line);

void dbcan_on_rx(transport_t *tp, uint8_t fifo, can_frame_t *msg);
void dbcan_on_tx(transport_t *tp, uint8_t marker, uint32_t timestamp);
void dbcan_on_fifo_event(transport_t *tp, uint8_t fifo, fifo_event_t event);
void dbcan_on_bus_event(transport_t *tp, bus_event_t event);

transport_error_t build_node_filters(dbcan_line_t *line);
transport_error_t init_can_line(dbcan_line_t *line);
transport_error_t deinit_can_line(dbcan_line_t *line);
transport_error_t change_filters(dbcan_line_t *line);
transport_error_t poll_line(dbcan_line_t *line);



#endif
