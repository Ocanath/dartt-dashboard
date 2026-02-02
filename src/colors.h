#ifndef COLORS_H
#define COLORS_H
#include <stdint.h>

#define NUM_COLORS 8

typedef struct rgb_t
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
}rgb_t;

extern const rgb_t template_colors[NUM_COLORS];

#endif // ! COLORS_H
