

/*
 * 11 bit can ID bitmask defines
*/

#define STD_MSG_TYPE_POS        9
#define STD_MSG_TYPE_MASK       (0b111 << STD_MSG_TYPE_POS)

/* reserved bit 8 for future use */ 

#define STD_UID_POS             0
#define STD_UID_MASK            (0b11111111 << STD_UID_POS)

/*
 * 29 bit can ID bitmask defines
*/

#define EXT_PRIO_POS            26
#define EXT_PRIO_MASK           (0b11 << EXT_PRIO_POS)

#define EXT_BROADCAST_POS       25
#define EXT_BROADCAST_MASK      (0b1 << EXT_BROADCAST_POS)

#define EXT_RESPONSE_POS        24
#define EXT_RESPONSE_MASK       (0b1 << EXT_RESPONSE_POS)

/* reserved bits 16-23 for future use */

#define EXT_SENDER_POS          8
#define EXT_SENDER_MASK         (0b11111111 << EXT_SENDER_POS)

#define EXT_RECEIVER_POS        0
#define EXT_RECEIVER_MASK       (0b11111111 << EXT_RECEIVER_POS)

/* 
 * message definitions
*/

typedef struct
{
    uint8_t msg_type;
    uint8_t uid;
} std_id_t;

typedef struct
{
    uint8_t prio;
    bool broadcast;
    bool responSe;
    uint8_t sender;
    uint8_t receiver;
} ext_id_t;


/*
 * Packing functions
*/

static inline uint16_t pack_std_id(std_id_t *id)                                                                                                                                                   
{                                                                                                                                                                                                 
    uint16_t raw = 0;
                                                                                                                                                                                                
    raw |= (id->msg_type << STD_MSG_TYPE_POS) & STD_MSG_TYPE_MASK;
    raw |= (id->uid << STD_UID_POS) & STD_UID_MASK;

    return raw;                                                                                                                                                                                   
}


static inline std_id_t unpack_std_id()
{

}