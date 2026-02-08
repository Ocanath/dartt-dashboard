/*
 * example_dartt.h
 *
 *  Created on: Feb 8, 2026
 *      Author: Ocanath Robotman
 */

#ifndef INC_EXAMPLE_DARTT_H_
#define INC_EXAMPLE_DARTT_H_
#include "stdint.h"

typedef struct mything_t
{
	float v[3];
	int32_t val;
}mything_t;



typedef struct example_dartt_t
{
	float a;
	int32_t b;
	uint32_t c;
	int16_t d1;
	int16_t d2;
	uint8_t exarr[8];
	mything_t thing1;

	mything_t thing_array[3];

}example_dartt_t;


extern example_dartt_t gl_example;

#endif /* INC_EXAMPLE_DARTT_H_ */
