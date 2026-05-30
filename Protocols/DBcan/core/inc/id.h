#ifndef ID_H
#define ID_H

#include <stdint.h>
#include <stdbool.h>

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

typedef struct {
    uint8_t     msg_type;
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

dbcan_id_t pack_id();

#endif