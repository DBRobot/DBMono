#ifndef REG_SERVER_H
#define REG_SERVER_H

#include <stdint.h>
#include "dbcan_reg_map.h"

/*
 * Register server: services read / write / command against the generated
 * reg_table + reg_store. Transport-agnostic -- the caller decodes a frame into
 * (register number, payload) and calls one of these; the response bytes / error
 * code go back out however the transport sends them.
 */

/* standard (global) error codes, 0..63; register-specific errors are >= 64 */
typedef enum {
    ERR_OK           = 0,
    ERR_NO_REGISTER  = 1,
    ERR_NOT_READABLE = 2,
    ERR_NOT_WRITABLE = 3,
    ERR_NOT_COMMAND  = 4,
    ERR_BAD_LENGTH   = 5,
    ERR_OUT_OF_RANGE = 6,
} reg_err_t;

reg_err_t reg_read(uint16_t n, uint8_t *out, uint16_t *out_len);
reg_err_t reg_write(uint16_t n, const uint8_t *val, uint16_t len);
reg_err_t reg_command(uint16_t n);

#endif
