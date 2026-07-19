/*
 * DRV8353.h
 *
 *  Created on: Jun 15, 2026
 *      Author: david-bascom
 */

#ifndef INC_DRV8353_H_
#define INC_DRV8353_H_

#include "stm32g4xx_hal.h"
#include "global_types.h"
#include <stdbool.h>

/*
 * ============================================================================
 * 									Register Map
 * ============================================================================
 */

#define DRV8353_FAULT_STATUS		0x00
#define DRV8353_VGS_STATUS			0x01
#define DRV8353_DRIVER_CONTROL		0x02
#define DRV8353_GATE_DRIVE_HS		0x03
#define DRV8353_GATE_DRIVE_LS		0x04
#define DRV8353_OCP_CONTROL			0x05
#define DRV8353_CSA_CONTROL			0x06

typedef union {
	struct {
		uint16_t VDS_LC		:1;
		uint16_t VDS_HC 	:1;
		uint16_t VDS_LB 	:1;
		uint16_t VDS_HB 	:1;
		uint16_t VDS_LA 	:1;
		uint16_t VDS_HA 	:1;
		uint16_t OTSD		:1;
		uint16_t UVLO		:1;
		uint16_t GDF		:1;
		uint16_t VDS_OCP	:1;
		uint16_t FAULT		:1;
	};
	uint16_t raw;
} drv8353_fault_status_t;

typedef union {
	struct {
		uint16_t VGS_LC		:1;
		uint16_t VGS_HC 	:1;
		uint16_t VGS_LB 	:1;
		uint16_t VGS_HB 	:1;
		uint16_t VGS_LA 	:1;
		uint16_t VGS_HA 	:1;
		uint16_t GDUV		:1;
		uint16_t OTW		:1;
		uint16_t SC_OC		:1;
		uint16_t SB_OC		:1;
		uint16_t SA_OC		:1;
	};
	uint16_t raw;
} drv8353_vgs_status_t;

typedef enum {
	DRV8353_OCP_SHUTDOWN_SINGLE_BRIDGE 	= 0b0,
	DRV8353_OCP_SHUTDOWN_ALL_BRIDGE		= 0b1,
} ocp_act_t;

typedef enum {
	DRV8353_PWM_MODE_6X		= 0b00,
	DRV8353_PWM_MODE_3X		= 0b01,
	DRV8353_PWM_MODE_1X 	= 0b10,
	DRV8353_PWM_MODE_INDP 	= 0b11,
} pwm_mode_t;

typedef enum {
	DRV8353_SYNC_ONE_PWM	= 0b0,
	DRV8353_ASYNC_ONE_PWM	= 0b1,
} one_pwm_com_t;

typedef union {
	struct {
		uint16_t CTL_FLT	:1;
		uint16_t BRAKE		:1;
		uint16_t COAST		:1;
		uint16_t PWM_DIR	:1;
		uint16_t PWM_COM	:1;
		uint16_t PWM_MODE	:2;
		uint16_t OTW_REP	:1;
		uint16_t DIS_GDF	:1;
		uint16_t DIS_GDUV	:1;
		uint16_t OCP_ACT	:1;
	};
	uint16_t raw;
} drv8353_driver_control_t;

typedef enum {
	DRV8353_IDRIVE_SRC_50MA   = 0b0000,	// 0b0001 also maps to 50mA
	DRV8353_IDRIVE_SRC_100MA  = 0b0010,
	DRV8353_IDRIVE_SRC_150MA  = 0b0011,
	DRV8353_IDRIVE_SRC_300MA  = 0b0100,
	DRV8353_IDRIVE_SRC_350MA  = 0b0101,
	DRV8353_IDRIVE_SRC_400MA  = 0b0110,
	DRV8353_IDRIVE_SRC_450MA  = 0b0111,
	DRV8353_IDRIVE_SRC_550MA  = 0b1000,
	DRV8353_IDRIVE_SRC_600MA  = 0b1001,
	DRV8353_IDRIVE_SRC_650MA  = 0b1010,
	DRV8353_IDRIVE_SRC_700MA  = 0b1011,
	DRV8353_IDRIVE_SRC_850MA  = 0b1100,
	DRV8353_IDRIVE_SRC_900MA  = 0b1101,
	DRV8353_IDRIVE_SRC_950MA  = 0b1110,
	DRV8353_IDRIVE_SRC_1000MA = 0b1111,
} drv8353_idrive_src_t;

typedef enum {
	DRV8353_IDRIVE_SNK_100MA  = 0b0000,	// 0b0001 also maps to 100mA
	DRV8353_IDRIVE_SNK_200MA  = 0b0010,
	DRV8353_IDRIVE_SNK_300MA  = 0b0011,
	DRV8353_IDRIVE_SNK_600MA  = 0b0100,
	DRV8353_IDRIVE_SNK_700MA  = 0b0101,
	DRV8353_IDRIVE_SNK_800MA  = 0b0110,
	DRV8353_IDRIVE_SNK_900MA  = 0b0111,
	DRV8353_IDRIVE_SNK_1100MA = 0b1000,
	DRV8353_IDRIVE_SNK_1200MA = 0b1001,
	DRV8353_IDRIVE_SNK_1300MA = 0b1010,
	DRV8353_IDRIVE_SNK_1400MA = 0b1011,
	DRV8353_IDRIVE_SNK_1700MA = 0b1100,
	DRV8353_IDRIVE_SNK_1800MA = 0b1101,
	DRV8353_IDRIVE_SNK_1900MA = 0b1110,
	DRV8353_IDRIVE_SNK_2000MA = 0b1111,
} drv8353_idrive_snk_t;

typedef union {
	struct {
		uint16_t IDRIVEN_HS	:4;
		uint16_t IDRIVEP_HS :4;
		uint16_t LOCK		:3;
	};
	uint16_t raw;
} drv8353_gate_drive_hs_t;

typedef enum {
	DRV8353_TDRIVE_500NS  = 0b00,
	DRV8353_TDRIVE_1000NS = 0b01,
	DRV8353_TDRIVE_2000NS = 0b10,
	DRV8353_TDRIVE_4000NS = 0b11,
} drv8353_tdrive_t;

/*
 * Only active when OCP_MODE = automatic retry (0b01) — ignored otherwise.
 */
typedef enum {
	DRV8353_CBC_CLEAR_AFTER_RETRY     = 0b0,	// VDS_OCP/SEN_OCP fault clears only after tRETRY elapses
	DRV8353_CBC_CLEAR_ON_PWM_OR_RETRY = 0b1,	// fault clears on the next new PWM edge, or after tRETRY, whichever comes first
} drv8353_cbc_t;

typedef union {
	struct {
		uint16_t IDRIVEN_LS	:4;
		uint16_t IDRIVEP_LS	:4;
		uint16_t TDRIVE		:2;
		uint16_t CBC		:1;
	};
	uint16_t raw;
} drv8353_gate_drive_ls_t;

typedef union {
	struct {
		uint16_t VDS_LVL	:4;
		uint16_t OCP_DEG	:2;
		uint16_t OCP_MODE	:2;
		uint16_t DEAD_TIME	:2;
		uint16_t TRETRY		:1;
	};
	uint16_t raw;
} drv8353_ocp_control_t;

typedef union {
	struct {
		uint16_t SEN_LVL	:2;
		uint16_t CSA_CAL_C	:1;
		uint16_t CSA_CAL_B	:1;
		uint16_t CSA_CAL_A	:1;
		uint16_t DIS_SEN	:1;
		uint16_t CSA_GAIN	:2;
		uint16_t LS_REF		:1;
		uint16_t VREF_DIV	:1;
		uint16_t CSA_FET	:1;
	};
	uint16_t raw;
} drv8353_csa_control_t;

/*
 * ============================================================================
 * 									 Structs
 * ============================================================================
 */

typedef struct {
	drv8353_driver_control_t	ctrl;
	drv8353_gate_drive_hs_t		gate_hs;
	drv8353_gate_drive_ls_t		gate_ls;
	drv8353_ocp_control_t		ocp_ctrl;
	drv8353_csa_control_t		csa_ctrl;
} drv8353_config_t;

typedef enum {
	DRV8353_OK = 0,
	DRV8353_SPI_ERR,
	DRV8353_FAULT
} drv8353_error_t;

typedef struct {
	drv8353_fault_status_t		status1;
	drv8353_vgs_status_t		status2;
} drv8353_status_t;

typedef struct {
	SPI_HandleTypeDef 	*handle;
	gpio_t 				cs;
	gpio_t 				enable;
	gpio_t 				fault;
} drv8353_t;

/*
 *
 * ============================================================================
 * 									Functions
 * ============================================================================
 *
 * Inits
 */
drv8353_error_t drv8353_init(drv8353_t *drive, drv8353_config_t *config);

/*
 * faults
 */
void drv8353_get_faults(drv8353_t *drive, drv8353_status_t *faults);
void drv8353_clear_faults(drv8353_t *drive);

/*
 * Read/Writes
 */
drv8353_error_t drv8353_read_reg(drv8353_t *drive, uint8_t addr, uint16_t *data);
drv8353_error_t drv8353_write_reg(drv8353_t *drive, uint8_t addr, uint16_t tx);
drv8353_error_t drv8353_write_bit(drv8353_t *drive, uint8_t addr, uint16_t bit, bool set);


#endif /* INC_DRV8353_H_ */
