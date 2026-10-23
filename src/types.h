#pragma once
#include <stdint.h>

typedef struct pixel_s
{
	uint8_t R, G, B;
} pixel_t;

typedef struct sprite_s
{
	unsigned short width, height;
	pixel_t *data;
} sprite_t;

typedef struct rect_s
{
	int x0, y0, x1, y1; /* x1 and y1 are exclusive */
} rect_t;
