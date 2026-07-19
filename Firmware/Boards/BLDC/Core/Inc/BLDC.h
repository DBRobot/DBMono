/*
 * BLDC.h
 *
 *  Created on: Jul 3, 2026
 *      Author: david-bascom
 */

#ifndef INC_BLDC_H_
#define INC_BLDC_H_

#include "DRV8353.h"

extern drv8353_t drive;
extern drv8353_config_t drive_cfg;

void bldc_init();

#endif /* INC_BLDC_H_ */
