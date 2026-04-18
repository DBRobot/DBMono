#ifndef CANFD_H
#define CANFD_H

#include "stm32g4xx.h"
#include <stdint.h>
#include <stdbool.h>

// TODO: add #ifdef for other STM32 families (H7, U5, etc.)
// G4 has fixed message RAM layout and element sizes.
// H7/U5 have configurable layout via SIDFC, XIDFC, RXF0C, RXESC, TXESC.
#define FDCAN_STD_FILTER_OFF    0x000
#define FDCAN_EXT_FILTER_OFF    0x070
#define FDCAN_RX_FIFO0_OFF      0x0B0
#define FDCAN_RX_FIFO1_OFF      0x188
#define FDCAN_TX_EVENT_OFF      0x260
#define FDCAN_TX_BUF_OFF        0x278
#define FDCAN_TX_ELEMENT_SIZE   72
#define FDCAN_RX_ELEMENT_SIZE   72
#define FDCAN_RX_FIFO0_COUNT    3
#define FDCAN_TX_BUF_COUNT      3

// Buffer element word 0
#define FDCAN_BUF_SID_Pos       18
#define FDCAN_BUF_SID_Msk       0x1FFC0000UL
#define FDCAN_BUF_EID_Msk       0x1FFFFFFFUL
#define FDCAN_BUF_XTD           (1UL << 30)
#define FDCAN_BUF_RTR           (1UL << 29)

// Buffer element word 1
#define FDCAN_BUF_DLC_Pos       16
#define FDCAN_BUF_DLC_Msk       0x000F0000UL
#define FDCAN_BUF_FDF           (1UL << 21)
#define FDCAN_BUF_BRS           (1UL << 20)

// Standard filter element
#define FDCAN_SFT_CLASSIC       (2UL << 30)
#define FDCAN_SFEC_FIFO0        (1UL << 27)
#define FDCAN_SFID1_Pos         16

// Extended filter element
#define FDCAN_EFT_CLASSIC       (2UL << 30)
#define FDCAN_EFEC_FIFO0        (1UL << 29)

typedef struct {                                                
      uint8_t prescaler;                                                                                                                                          
      uint8_t tseg1;                                              
      uint8_t tseg2;                                                                                                                                              
      uint8_t sjw;
      uint8_t delay_comp_offset;
      uint8_t delay_comp_filter;
} fdcan_btr_t;

typedef struct {
    uint32_t id;
    uint32_t mask;
    uint8_t type;
} fdcan_filter_t;

typedef struct {
    uint32_t id;
    uint8_t  data[64];
    uint8_t  len;
    bool     ext;
} fdcan_msg_t;

typedef struct
{
    FDCAN_GlobalTypeDef     *instance;
    fdcan_btr_t             nom;
    fdcan_btr_t             data;
    fdcan_filter_t          *filters;
    uint8_t                 num_filters;
    bool                    enable_fd;
    bool                    enable_brs;
    bool                    tx_queue_mode;
    bool                    disable_auto_retx;
    bool                    enable_loopback;
} fdcan_config_t;

int fdcan_init(const fdcan_config_t *config);

int fdcan_tx(const fdcan_config_t *config, const fdcan_msg_t *msg);

int fdcan_rx(const fdcan_config_t *config, fdcan_msg_t *msg);

#endif