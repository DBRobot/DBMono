#ifndef ID_H
#define ID_H

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/*
 * 11 bit frames:
 * 
 *  10  9  8  7  6  5  4  3  2  1  0
 * [MTYPE][R][-------SRC UID-------]
 * 
 * 29 bit frames:
 * 
 * 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * [-PRIO-][B][R] [------Reserved-------] [------Sender UID------] [----Receiver UID----]
*/

#define DBCAN_11BIT_ID_MTYPE_Pos            9u
#define DBCAN_11BIT_ID_MTYPE_Mask           (0x03u << DBCAN_11BIT_ID_MTYPE_Pos)
#define DBCAN_11BIT_ID_RESP_Pos             8u
#define DBCAN_11BIT_ID_RESP_Mask            (0x01u << DBCAN_11BIT_ID_RESP_Pos)
#define DBCAN_11BIT_ID_UID_Pos              0u
#define DBCAN_11BIT_ID_UID_Mask             (0xFFu << DBCAN_11BIT_ID_UID_Pos)

#define DBCAN_29BIT_ID_PRIO_Pos             26U
#define DBCAN_29BIT_ID_PRIO_Mask            (0x07u << DBCAN_29BIT_ID_PRIO_Pos)
#define DBCAN_29BIT_ID_BCAST_Pos            25U
#define DBCAN_29BIT_ID_BCAST_Mask           (0x01u << DBCAN_29BIT_ID_BCAST_Pos)
#define DBCAN_29BIT_ID_RESP_Pos             24U
#define DBCAN_29BIT_ID_RESP_Mask            (0x01u << DBCAN_29BIT_ID_RESP_Pos) 
#define DBCAN_29BIT_ID_SUID_Pos             8U
#define DBCAN_29BIT_ID_SUID_Mask            (0xFFu << DBCAN_29BIT_ID_SUID_Pos)
#define DBCAN_29BIT_ID_RUID_Pos             0U
#define DBCAN_29BIT_ID_RUID_Mask            (0xFFu << DBCAN_29BIT_ID_RUID_Pos)

typedef enum {
    DBCAN_ERROR,
    DBCAN_NET_MANAGEMENT,
    DBCAN_DEBUG,
    DBCAN_HEARTBEAT,
} msg_t;

typedef struct {
    msg_t       msg_type;
    bool        responce_flag;
    uint8_t     src_uid;
} short_id_t;

typedef struct {
    uint8_t     prio;
    bool        bcast_flag;
    bool        responce_flag;
    uint8_t     sender_uid;
    uint8_t     reciever_uid;
} long_id_t;

typedef enum {
    DBCAN_ID_SHORT,
    DBCAN_ID_LONG,
} dbcan_id_kind_t;

typedef struct {
    dbcan_id_kind_t     kind;
    union {
        short_id_t      short_id;
        long_id_t       long_id;
    } u;
} dbcan_id_t;

static inline uint32_t pack_id(const dbcan_id_t *id) {
    switch(id->kind) {
        case DBCAN_ID_SHORT: {
            const short_id_t s = id->u.short_id;
            return ((uint32_t)s.msg_type            << DBCAN_11BIT_ID_MTYPE_Pos)
                 | ((uint32_t)s.responce_flag       << DBCAN_11BIT_ID_RESP_Pos)
                 | ((uint32_t)s.src_uid             << DBCAN_11BIT_ID_UID_Pos);
        }
        case DBCAN_ID_LONG: {
            const long_id_t l = id->u.long_id;
            return ((uint32_t)l.prio                << DBCAN_29BIT_ID_PRIO_Pos)
                 | ((uint32_t)l.bcast_flag          << DBCAN_29BIT_ID_BCAST_Pos)
                 | ((uint32_t)l.responce_flag       << DBCAN_29BIT_ID_RESP_Pos)
                 | ((uint32_t)l.sender_uid          << DBCAN_29BIT_ID_SUID_Pos)
                 | ((uint32_t)l.reciever_uid        << DBCAN_29BIT_ID_RUID_Pos);
        }
        default:
            assert(false);
            return 0;
    }
}

static inline dbcan_id_t unpack_id(const uint32_t raw, bool is_ext) {
    dbcan_id_t id;
    if(is_ext) {
        id.kind = DBCAN_ID_LONG;
        id.u.long_id = (long_id_t){
            .prio               = (raw & DBCAN_29BIT_ID_PRIO_Mask) >> DBCAN_29BIT_ID_PRIO_Pos,
            .bcast_flag         = (raw & DBCAN_29BIT_ID_BCAST_Mask) >> DBCAN_29BIT_ID_BCAST_Pos,
            .responce_flag      = (raw & DBCAN_29BIT_ID_RESP_Mask) >> DBCAN_29BIT_ID_RESP_Pos,
            .sender_uid         = (raw & DBCAN_29BIT_ID_SUID_Mask) >> DBCAN_29BIT_ID_SUID_Pos,
            .reciever_uid       = (raw & DBCAN_29BIT_ID_RUID_Mask) >> DBCAN_29BIT_ID_RUID_Pos,
        };
    } else {
        id.kind = DBCAN_ID_SHORT;
        id.u.short_id = (short_id_t){
            .msg_type           = (raw & DBCAN_11BIT_ID_MTYPE_Mask) >> DBCAN_11BIT_ID_MTYPE_Pos,
            .responce_flag      = (raw & DBCAN_11BIT_ID_RESP_Mask) >> DBCAN_11BIT_ID_RESP_Pos,
            .src_uid            = (raw & DBCAN_11BIT_ID_UID_Mask) >> DBCAN_11BIT_ID_UID_Pos,
        };
    }

    return id;
}

#endif