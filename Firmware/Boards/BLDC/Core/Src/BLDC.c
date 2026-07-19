/*
 * BLDC.c
 *
 *  Created on: Jul 3, 2026
 *      Author: david-bascom
 */


drv8353_t drive = {
		.handle		= &hspi1,
		.cs			= { .port = DRIVER_CSN_GPIO_Port, .pin = DRIVER_CSN_Pin },
		.enable		= {	.port = ENABLE_GPIO_Port, .pin = ENABLE_Pin },
		.fault		= { .port = nFAULT_GPIO_Port, .pin = nFAULT_Pin },
};
