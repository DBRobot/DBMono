#include "canfd.h"
#include <string.h>

static uint8_t dlc_to_len(uint8_t dlc) {
    if (dlc <= 8)  return dlc;
    if (dlc == 9)  return 12;
    if (dlc == 10) return 16;
    if (dlc == 11) return 20;
    if (dlc == 12) return 24;
    if (dlc == 13) return 32;
    if (dlc == 14) return 48;
    return 64;
}

static uint8_t len_to_dlc(uint8_t len) {
    if (len <= 8)  return len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

int fdcan_init(const fdcan_config_t *config) {

    FDCAN_GlobalTypeDef *fdcan = config->instance;
    
    // start the can peripheral clock and wait for it to start
    fdcan->CCCR &= ~FDCAN_CCCR_CSR;
    while(fdcan->CCCR & FDCAN_CCCR_CSA);

    // enter INIT mode
    fdcan->CCCR |= FDCAN_CCCR_INIT;
    while (!(fdcan->CCCR & FDCAN_CCCR_INIT));

    // unlock the and set the config
    fdcan->CCCR |= FDCAN_CCCR_CCE;
    fdcan->NBTP = (config->nom.sjw          << FDCAN_NBTP_NSJW_Pos)                                                                                                       
                | (config->nom.prescaler    << FDCAN_NBTP_NBRP_Pos)    
                | (config->nom.tseg1        << FDCAN_NBTP_NTSEG1_Pos)                                                                                                     
                | (config->nom.tseg2        << FDCAN_NBTP_NTSEG2_Pos);  
    fdcan->DBTP = (config->data.sjw         << FDCAN_DBTP_DSJW_Pos)
                | (config->data.prescaler   << FDCAN_DBTP_DBRP_Pos)    
                | (config->data.tseg1       << FDCAN_DBTP_DTSEG1_Pos)                                                                                                     
                | (config->data.tseg2       << FDCAN_DBTP_DTSEG2_Pos); 
    
    // set delay compensation
    if (config->data.delay_comp_offset) {                                                                                                                                  
        fdcan->TDCR = (config->data.delay_comp_offset << FDCAN_TDCR_TDCO_Pos)
                    | (config->data.delay_comp_filter << FDCAN_TDCR_TDCF_Pos);                                                                                              
        fdcan->DBTP |= FDCAN_DBTP_TDC;
    }       

    // set up filters
    uint32_t *std_filter = (uint32_t *)(SRAMCAN_BASE + FDCAN_STD_FILTER_OFF);
    uint32_t *ext_filter = (uint32_t *)(SRAMCAN_BASE + FDCAN_EXT_FILTER_OFF);                                                                                                       
    uint8_t std_idx = 0;                                                                                                                                            
    uint8_t ext_idx = 0;                                                                                                                                            
                                                                                                                                                                    
    for (uint8_t i = 0; i < config->num_filters; i++) {
        if (config->filters[i].type == 0) {
            if (std_idx >= 28) return -1;
            std_filter[std_idx++] = FDCAN_SFT_CLASSIC
                                  | FDCAN_SFEC_FIFO0
                                  | (config->filters[i].id << FDCAN_SFID1_Pos)
                                  | (config->filters[i].mask);
        } else {
            if (ext_idx >= 8) return -1;
            ext_filter[ext_idx * 2]     = FDCAN_EFEC_FIFO0
                                        | (config->filters[i].id);
            ext_filter[ext_idx * 2 + 1] = FDCAN_EFT_CLASSIC
                                        | (config->filters[i].mask);
            ext_idx++;
        }
    }                                                                                                                                                               
                                                                    
    fdcan->RXGFC = FDCAN_RXGFC_RRFE
                | FDCAN_RXGFC_RRFS                                                                                                                                 
                | (2 << FDCAN_RXGFC_ANFE_Pos)  
                | (2 << FDCAN_RXGFC_ANFS_Pos)                                                                                                                      
                | (std_idx << FDCAN_RXGFC_LSS_Pos)                 
                | (ext_idx << FDCAN_RXGFC_LSE_Pos); 

    // set tx mode (FIFO or Queue)
    if (config->tx_queue_mode)
        fdcan->TXBC = FDCAN_TXBC_TFQM;

    // enable FD and bit rate switching
    if (config->enable_fd) {
        fdcan->CCCR |= FDCAN_CCCR_FDOE;
        if (config->enable_brs)
            fdcan->CCCR |= FDCAN_CCCR_BRSE;
    } 
    
    // set auto retransmittion on/off
    if (config->disable_auto_retx) {                                                                                                                                
        fdcan->CCCR |= FDCAN_CCCR_DAR;                              
    }

    // set loopback mode on/off
    if (config->enable_loopback) {
        fdcan->CCCR |= FDCAN_CCCR_TEST;
        fdcan->TEST |= FDCAN_TEST_LBCK;                                                                                                                             
    }
                                                                                                                                                                 
    // leave INIT mode                                                                                                                                              
    fdcan->CCCR &= ~FDCAN_CCCR_INIT;                                                                                                                                
    while (fdcan->CCCR & FDCAN_CCCR_INIT);                                                                                                                          
                                                                    
    return 0;
}

int fdcan_deinit(FDCAN_GlobalTypeDef *instance) {
    RCC->APB1RSTR1 |= RCC_APB1RSTR1_FDCANRST;
    RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_FDCANRST;

    return 0;
}

int fdcan_tx(const fdcan_config_t *config, const fdcan_msg_t *msg) {

    FDCAN_GlobalTypeDef *fdcan = config->instance;

    // check if all tx buffers are full
    if ((fdcan->TXFQS & FDCAN_TXFQS_TFQF) != 0)
        return -1;

    // get next free tx buffer index
    uint8_t put_idx = (fdcan->TXFQS >> FDCAN_TXFQS_TFQPI_Pos) & 0x3;
    uint32_t *tx_buf = (uint32_t *)(SRAMCAN_BASE + FDCAN_TX_BUF_OFF + (put_idx * FDCAN_TX_ELEMENT_SIZE));

    // set ID
    if (msg->ext)
        tx_buf[0] = msg->id | FDCAN_BUF_XTD;
    else
        tx_buf[0] = (msg->id << FDCAN_BUF_SID_Pos);

    // set DLC, FD format, BRS
    tx_buf[1] = (len_to_dlc(msg->len) << FDCAN_BUF_DLC_Pos)
              | (config->enable_fd  ? FDCAN_BUF_FDF : 0)
              | (config->enable_brs ? FDCAN_BUF_BRS : 0);
    memcpy(&tx_buf[2], msg->data, msg->len);

    // request transmission
    fdcan->TXBAR = (1 << put_idx);

    return 0;
}

int fdcan_rx(const fdcan_config_t *config, fdcan_msg_t *msg) {

    FDCAN_GlobalTypeDef *fdcan = config->instance;

    // check if FIFO 0 has a message
    uint8_t fill = fdcan->RXF0S & FDCAN_RXF0S_F0FL;
    if (fill == 0)
        return -1;

    // get the read index
    uint8_t get_idx = (fdcan->RXF0S >> FDCAN_RXF0S_F0GI_Pos) & 0x3;
    uint32_t *rx_buf = (uint32_t *)(SRAMCAN_BASE + FDCAN_RX_FIFO0_OFF + (get_idx * FDCAN_RX_ELEMENT_SIZE));

    // read ID
    if (rx_buf[0] & FDCAN_BUF_XTD) {
        msg->id  = rx_buf[0] & FDCAN_BUF_EID_Msk;
        msg->ext = true;
    } else {
        msg->id  = (rx_buf[0] & FDCAN_BUF_SID_Msk) >> FDCAN_BUF_SID_Pos;
        msg->ext = false;
    }

    // read length from DLC
    msg->len = dlc_to_len((rx_buf[1] & FDCAN_BUF_DLC_Msk) >> FDCAN_BUF_DLC_Pos);

    // copy data
    memcpy(msg->data, &rx_buf[2], msg->len);
    fdcan->RXF0A = get_idx;

    return 0;
}