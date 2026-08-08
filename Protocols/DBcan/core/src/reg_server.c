/*
 * Register server -- services register access against the generated
 * reg_table + reg_store. Knows nothing about frames or transports; the caller
 * has already decoded a frame into (register number, payload).
 */

#include <string.h>

#include "reg_server.h"


reg_err_t reg_read(uint16_t n, uint8_t *out, uint16_t *out_len)
{
    int i = reg_index(n);
    if (i < 0) {
        return ERR_NO_REGISTER;
    }

    const reg_desc_t *d = &reg_table[i];

    if (!(d->perms & REG_R)) {
        return ERR_NOT_READABLE;
    }

    /* out_len arrives holding the caller's buffer size; a short buffer is an
     * error rather than a truncated read, so the caller cannot mistake a
     * partial value for a whole one. */
    if (*out_len < d->width_bytes) {
        return ERR_BAD_LENGTH;
    }

    memcpy(out, &reg_store[d->offset], d->width_bytes);
    *out_len = d->width_bytes;

    return ERR_OK;
}


/*
 * Little-endian raw value of a payload, sign-extended for DTYPE_INT.
 * Only called for bounded int/uint registers, whose bounds the compiler has
 * already proved fit in int32_t.
 */
static int32_t decode_raw(const uint8_t *val, const reg_desc_t *d)
{
    uint32_t raw = 0;
    for (uint16_t i = 0; i < d->width_bytes; i++) {
        raw |= (uint32_t)val[i] << (8u * i);
    }

    if (d->dtype == DTYPE_INT && d->width_bytes < 4) {
        const uint32_t sign = 1u << (8u * d->width_bytes - 1u);
        if (raw & sign) {
            raw |= ~((1u << (8u * d->width_bytes)) - 1u);   /* sign extend */
        }
    }

    return (int32_t)raw;
}


reg_err_t reg_write(uint16_t n, const uint8_t *val, uint16_t len)
{
    int i = reg_index(n);
    if (i < 0) {
        return ERR_NO_REGISTER;
    }

    const reg_desc_t *d = &reg_table[i];

    if (!(d->perms & REG_W)) {
        return ERR_NOT_WRITABLE;
    }

    /* exact, not a capacity: a short write would leave half a value behind */
    if (len != d->width_bytes) {
        return ERR_BAD_LENGTH;
    }

    /* min > max is the compiler's sentinel for "unbounded"; groups carry
     * DTYPE_NONE and are bounded by their effective permission instead */
    if (d->min <= d->max && (d->dtype == DTYPE_INT || d->dtype == DTYPE_UINT)) {
        const int32_t raw = decode_raw(val, d);
        if (raw < d->min || raw > d->max) {
            return ERR_OUT_OF_RANGE;
        }
    }

    memcpy(&reg_store[d->offset], val, len);
    dbcan_on_write(n);

    return ERR_OK;
}


__attribute__((weak)) void dbcan_on_write(uint16_t n)
{
    (void)n;
}


reg_err_t reg_command(uint16_t n)
{
    int i = reg_index(n);
    if (i < 0) {
        return ERR_NO_REGISTER;
    }

    if (!(reg_table[i].perms & REG_CMD) || cmd_table[i] == NULL) {
        return ERR_NOT_COMMAND;
    }

    /* the handler is whatever won at link time: the library's definition for
     * a protocol command, a board's for its own, or the generated weak
     * default returning ERR_NOT_IMPLEMENTED */
    return cmd_table[i]();
}
