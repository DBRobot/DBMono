#ifndef HEADER_H
#define HEADER_H

#include <stdint.h>
#include <stdbool.h>

#include "id.h"

/*
 * DBcan service-plane data payload (CAN FD)
 *
 * All multi-byte fields are little-endian (LSB first), packed with no padding.
 * Register width is fixed by the register map; both ends know it, so the
 * payload carries no length, no type tag, and no register id on replies.
 *
 * This is a point-to-point (29-bit ID) plane. The request/response role lives
 * in the CAN ID (see id.h, PTP ROLE bit), not the payload -- it is known before
 * the payload is parsed, so hardware filters can route requests and responses
 * into separate RX FIFOs. Responses are dominant (ROLE=0) so completing
 * transactions win arbitration within a priority level.
 *
 * byte 0        header (see below)
 * byte 1..2     register id, 16-bit          (request frames only)
 * byte 3..N     value, register-width bits    (writes and read replies)
 *
 * Header byte (byte 0):
 *
 *  7    6   5   4   3   2   1   0
 * [-] [A/E] [S] [------REQ_ID------]
 *
 *    [7]        reserved, must be 0
 *    [6]        role is read from the CAN ID (see id.h):
 *               request  (ROLE=1): A = ack requested, reply wanted
 *               response (ROLE=0): E = error, payload is {code, name}
 * S  [5]        seg    multiframe: value continues, append until map width met
 * REQ_ID [4:0]         matches a reply to its request; scoped per peer pair,
 *                      rejects stale replies after a timeout (32 outstanding).
 *                      ignored when A=0 (fire-and-forget); send 0.
 *                      the receiver also remembers the last REQ_ID it acted on
 *                      per peer, so a retried command is acked but not re-run.
 *
 * Frame contents by operation (ROLE = CAN ID bit; register id omitted on all
 * replies):
 *
 *   op           ROLE  A/E  S   bytes
 *   read   req    1     1   0   [hdr][id]
 *   read   rsp    0     0   0   [hdr][value]
 *   write  req    1    0/1  0   [hdr][id][value]
 *   write  ack    0     0   0   [hdr]
 *   cmd    req    1    0/1  0   [hdr][id]
 *   cmd    ack    0     0   0   [hdr]
 *   error  rsp    0     1   0   [hdr][code]
 *
 * Operation is implied, not encoded: resolve the register's permission class
 * first; CMD -> command (value ignored), else 0 value bytes -> read,
 * N value bytes -> write.
 *
 * Multiframe (S = 1): segments share one CAN ID so arrival order is the send
 * order; value bytes are appended in order. No sequence number and no total-
 * length field -- completion is when the assembled length reaches the map's
 * declared register width; a short count on timeout means retransmit the
 * whole transfer.
*/

#define DBCAN_HDR_FLAG_Pos                  6U
#define DBCAN_HDR_FLAG_Mask                 (0x01u << DBCAN_HDR_FLAG_Pos)
#define DBCAN_HDR_SEG_Pos                   5U
#define DBCAN_HDR_SEG_Mask                  (0x01u << DBCAN_HDR_SEG_Pos)
#define DBCAN_HDR_REQID_Pos                 0U
#define DBCAN_HDR_REQID_Mask                (0x1Fu << DBCAN_HDR_REQID_Pos)

typedef struct {
    bool        ack;
    bool        err;
    bool        seg;
    uint8_t     req_id;
} dbcan_hdr_t;

static inline uint8_t pack_hdr(const dbcan_hdr_t *h, role_t role) {
    const bool flag = (role == DBCAN_ROLE_REQUEST) ? h->ack : h->err;
    return (((uint8_t)flag          << DBCAN_HDR_FLAG_Pos)  & DBCAN_HDR_FLAG_Mask)
         | (((uint8_t)h->seg        << DBCAN_HDR_SEG_Pos)   & DBCAN_HDR_SEG_Mask)
         | (((uint8_t)h->req_id     << DBCAN_HDR_REQID_Pos) & DBCAN_HDR_REQID_Mask);
}

static inline dbcan_hdr_t unpack_hdr(const uint8_t raw, role_t role) {
    const bool flag = (raw & DBCAN_HDR_FLAG_Mask) >> DBCAN_HDR_FLAG_Pos;
    dbcan_hdr_t h = {
        .ack        = (role == DBCAN_ROLE_REQUEST)  ? flag : false,
        .err        = (role == DBCAN_ROLE_RESPONSE) ? flag : false,
        .seg        = (raw & DBCAN_HDR_SEG_Mask)   >> DBCAN_HDR_SEG_Pos,
        .req_id     = (raw & DBCAN_HDR_REQID_Mask) >> DBCAN_HDR_REQID_Pos,
    };
    return h;
}

#endif
