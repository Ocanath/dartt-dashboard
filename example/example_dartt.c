/*
 * example_dartt.c
 *
 *  Created on: Feb 8, 2026
 *      Author: Ocanath Robotman
 */

#include "example_dartt.h"

example_dartt_t gl_example =
{
		.a = 1.2345f,
		.b = -12345,
		.c = 34345,
		.d1 = 14,
		.d2 = 15,
		.exarr = {
				1,
				2,
				3,
				4,
				5,
				6,
				7,
				8
		},
		.thing1 = {
				.v = {
						2.34f,
						5.67f,
						8.910f,
				},
				.val = -5
		},
		.thing_array =
		{
				{
						.v = {
								-1.,
								-2.,
								-3.
						},
						.val = -6
				},
				{
						.v = {
								-4.,
								-5.,
								-6.
						},
						.val = -7
				},
				{
						.v = {
								-7.,
								-8.,
								-9.
						},
						.val = -8
				}
		}
};
