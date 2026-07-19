/*
 * DRV8353.c
 *
 *  Created on: Jun 15, 2026
 *      Author: david-bascom
 */

#include "DRV8353.h"

/*
 * ============================================================================
 * 									 Init
 * ============================================================================
 */

drv8353_error_t drv8353_init(drv8353_t *drive, drv8353_config_t *config) {
	HAL_GPIO_WritePin(drive->enable.port, drive->enable.pin, GPIO_PIN_SET);
	HAL_Delay(2);
	if(!HAL_GPIO_ReadPin(drive->fault.port, drive->fault.pin)) return DRV8353_FAULT;

	drv8353_write_reg(drive, DRV8353_DRIVER_CONTROL, config->ctrl.raw);
	drv8353_write_reg(drive, DRV8353_GATE_DRIVE_HS, config->gate_hs.raw);
	drv8353_write_reg(drive, DRV8353_GATE_DRIVE_LS, config->gate_ls.raw);
	drv8353_write_reg(drive, DRV8353_OCP_CONTROL, config->ocp_ctrl.raw);
	drv8353_write_reg(drive, DRV8353_CSA_CONTROL, config->csa_ctrl.raw);

	if(!HAL_GPIO_ReadPin(drive->fault.port, drive->fault.pin)) return DRV8353_FAULT;

	return DRV8353_OK;
}

/*
 * ============================================================================
 * 									 Faults
 * ============================================================================
 */

void drv8353_get_faults(drv8353_t *drive, drv8353_status_t *faults) {
	drv8353_read_reg(drive, DRV8353_FAULT_STATUS, &faults->status1.raw);
	drv8353_read_reg(drive, DRV8353_VGS_STATUS, &faults->status2.raw);
}

void drv8353_clear_faults(drv8353_t *drive) {
	drv8353_driver_control_t mask = {0};
	mask.CTL_FLT = 1;
	drv8353_write_bit(drive, DRV8353_DRIVER_CONTROL, mask.raw, true);
}


/*
 * ============================================================================
 * 									Read/Write
 * ============================================================================
 */

drv8353_error_t drv8353_read_reg(drv8353_t *drive, uint8_t addr, uint16_t *data) {
	uint16_t frame = 0;
	frame |= (1u << 15);
	frame |= (addr << 11);
	HAL_GPIO_WritePin(drive->cs.port, drive->cs.pin, GPIO_PIN_RESET);
	if(HAL_SPI_TransmitReceive(drive->handle, (uint8_t *)&frame, (uint8_t *)data, 1, HAL_MAX_DELAY) != HAL_OK) return DRV8353_SPI_ERR;
	HAL_GPIO_WritePin(drive->cs.port, drive->cs.pin, GPIO_PIN_SET);
	*data &= 0x7FF;

	return HAL_OK;
}

drv8353_error_t drv8353_write_reg(drv8353_t *drive, uint8_t addr, uint16_t tx) {
	uint16_t frame= tx;
	frame &= ~(1u << 15);
	frame &= ~(0xFu << 11);
	frame |= (addr << 11);

	uint16_t data;
	HAL_GPIO_WritePin(drive->cs.port, drive->cs.pin, GPIO_PIN_RESET);
	if(HAL_SPI_TransmitReceive(drive->handle, (uint8_t *)&frame, (uint8_t *)&data, 1, HAL_MAX_DELAY) != HAL_OK) return DRV8353_SPI_ERR;
	HAL_GPIO_WritePin(drive->cs.port, drive->cs.pin, GPIO_PIN_SET);

	return DRV8353_OK;
}

drv8353_error_t drv8353_write_bit(drv8353_t *drive, uint8_t addr, uint16_t mask, bool set) {
	uint16_t reg_val;
	drv8353_read_reg(drive, addr, &reg_val);
	if (set) {
		reg_val |= mask;
	} else reg_val &= ~mask;
	drv8353_write_reg(drive, addr, reg_val);

	return DRV8353_OK;
}










