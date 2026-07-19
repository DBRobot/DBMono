/*
 * global_types.h
 *
 *  Created on: Jul 3, 2026
 *      Author: david-bascom
 */

#ifndef INC_GLOBAL_TYPES_H_
#define INC_GLOBAL_TYPES_H_

typedef struct {
	GPIO_TypeDef	*port;
	uint16_t 		pin;
}gpio_t;

#endif /* INC_GLOBAL_TYPES_H_ */
