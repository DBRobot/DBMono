#ifndef INTERNAL_REG_OPS_H
#define INTERNAL_REG_OPS_H

#include <string.h>
#include "dbcan_reg_map.h"


static inline uint16_t get_reg_val(uint16_t n, void *out) {
    int i = reg_index(n);
    if (i < 0) return 0;

    const reg_desc_t *d = &reg_table[i];
    memcpy(out, (uint8_t *)&reg_store + d->offset, d->width_bytes);
    return d->width_bytes;
}

static inline uint16_t set_reg_val(uint16_t n, const void *in) {
    int i = reg_index(n);
    if (i < 0) return 0;

    const reg_desc_t *d = &reg_table[i];
    memcpy((uint8_t *)&reg_store + d->offset, in, d->width_bytes);
    return d->width_bytes;
}

#endif
