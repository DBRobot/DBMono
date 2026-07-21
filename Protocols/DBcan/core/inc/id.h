#ifndef ID_H
#define ID_H

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/*
 * 11 bit frames (broadcast):
 *
 *  10  9  8  7  6  5  4  3  2  1  0
 * [-PRIO--][-------SRC UID--------]
 *
 * 29 bit frames (point-to-point):
 *
 * 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * [-PRIO--][--------Unallocated---------][------Sender UID------] [----Receiver UID-----]
*/

#define DBCAN_11BIT_ID_PRIO_Pos             8u
#define DBCAN_11BIT_ID_PRIO_Mask            (0x07u << DBCAN_11BIT_ID_PRIO_Pos)
#define DBCAN_11BIT_ID_UID_Pos              0u
#define DBCAN_11BIT_ID_UID_Mask             (0xFFu << DBCAN_11BIT_ID_UID_Pos)

#define DBCAN_29BIT_ID_PRIO_Pos             26U
#define DBCAN_29BIT_ID_PRIO_Mask            (0x07u << DBCAN_29BIT_ID_PRIO_Pos)
#define DBCAN_29BIT_ID_SUID_Pos             8U
#define DBCAN_29BIT_ID_SUID_Mask            (0xFFu << DBCAN_29BIT_ID_SUID_Pos)
#define DBCAN_29BIT_ID_RUID_Pos             0U
#define DBCAN_29BIT_ID_RUID_Mask            (0xFFu << DBCAN_29BIT_ID_RUID_Pos)

typedef enum {
    DBCAN_PRIO_EMERGENCY    = 0,
    DBCAN_PRIO_NMT_SYNC     = 1,
    DBCAN_PRIO_CONTROL      = 2,
    DBCAN_PRIO_EVENTS       = 3,
    DBCAN_PRIO_TELEMETRY    = 4,
    DBCAN_PRIO_SERVICE      = 5,
    DBCAN_PRIO_DEBUG        = 6,
    DBCAN_PRIO_BULK         = 7,
} prio_t;

typedef struct {
    prio_t      prio;
    uint8_t     src_uid;
} bcast_id_t;

typedef struct {
    prio_t      prio;
    uint8_t     sender_uid;
    uint8_t     receiver_uid;
} ptp_id_t;

typedef enum {
    DBCAN_ID_BCAST,
    DBCAN_ID_PTP,
} dbcan_id_kind_t;

typedef struct {
    dbcan_id_kind_t     kind;
    union {
        bcast_id_t      bcast_id;
        ptp_id_t        ptp_id;
    } u;
} dbcan_id_t;

static inline uint32_t pack_id(const dbcan_id_t *id) {
    switch(id->kind) {
        case DBCAN_ID_BCAST: {
            const bcast_id_t b = id->u.bcast_id;
            return (((uint32_t)b.prio           << DBCAN_11BIT_ID_PRIO_Pos) & DBCAN_11BIT_ID_PRIO_Mask)
                 | (((uint32_t)b.src_uid        << DBCAN_11BIT_ID_UID_Pos)  & DBCAN_11BIT_ID_UID_Mask);
        }
        case DBCAN_ID_PTP: {
            const ptp_id_t p = id->u.ptp_id;
            return (((uint32_t)p.prio           << DBCAN_29BIT_ID_PRIO_Pos) & DBCAN_29BIT_ID_PRIO_Mask)
                 | (((uint32_t)p.sender_uid     << DBCAN_29BIT_ID_SUID_Pos) & DBCAN_29BIT_ID_SUID_Mask)
                 | (((uint32_t)p.receiver_uid   << DBCAN_29BIT_ID_RUID_Pos) & DBCAN_29BIT_ID_RUID_Mask);
        }
        default:
            assert(false);
            return 0;
    }
}

static inline dbcan_id_t unpack_id(const uint32_t raw, bool is_ext) {
    dbcan_id_t id;
    if(is_ext) {
        id.kind = DBCAN_ID_PTP;
        id.u.ptp_id = (ptp_id_t){
            .prio               = (raw & DBCAN_29BIT_ID_PRIO_Mask) >> DBCAN_29BIT_ID_PRIO_Pos,
            .sender_uid         = (raw & DBCAN_29BIT_ID_SUID_Mask) >> DBCAN_29BIT_ID_SUID_Pos,
            .receiver_uid       = (raw & DBCAN_29BIT_ID_RUID_Mask) >> DBCAN_29BIT_ID_RUID_Pos,
        };
    } else {
        id.kind = DBCAN_ID_BCAST;
        id.u.bcast_id = (bcast_id_t){
            .prio               = (raw & DBCAN_11BIT_ID_PRIO_Mask) >> DBCAN_11BIT_ID_PRIO_Pos,
            .src_uid            = (raw & DBCAN_11BIT_ID_UID_Mask)  >> DBCAN_11BIT_ID_UID_Pos,
        };
    }

    return id;
}

#endif
