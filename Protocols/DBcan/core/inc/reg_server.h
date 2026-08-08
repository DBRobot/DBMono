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

/* reg_err_t and the command handler prototypes come from dbcan_reg_map.h --
 * the error codes are protocol constants and the handler set follows the map. */

/*
 * Read a register's stored bytes into out.
 *
 * out_len is in/out: set it to the capacity of out before the call, and on
 * ERR_OK it holds the number of bytes written. A buffer smaller than the
 * register's width returns ERR_BAD_LENGTH and writes nothing. Size buffers
 * with REG_MAX_WIDTH to be able to hold any register in the map.
 */
reg_err_t reg_read(uint16_t n, uint8_t *out, uint16_t *out_len);
reg_err_t reg_write(uint16_t n, const uint8_t *val, uint16_t len);
reg_err_t reg_command(uint16_t n);

/*
 * Called after a write has landed in the store, and only on success -- a
 * rejected write never fires it. n is the register that was written, which
 * for a group write is the group, not its children.
 *
 * Notification, not veto: the value is already stored by the time this runs.
 * The weak default does nothing; define it normally to react.
 */
void dbcan_on_write(uint16_t n);

#endif
