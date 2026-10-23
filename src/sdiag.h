#pragma once

#include <stdint.h>

typedef enum
{
	DIALOG_ICON_INFO = 0,
	DIALOG_ICON_WARNING,
	DIALOG_ICON_ERROR
} DialogIcon;

typedef struct pixel_s
{
	uint8_t R, G, B;
} pixel_t;

typedef struct sprite_s
{
	unsigned short width, height;
	pixel_t *data;
} sprite_t;

typedef struct font_pixel_s
{
	unsigned char R;
} pixel_t;

typedef struct font_sprite_s
{
	unsigned short width, height;
	pixel_t *data;
} sprite_t;

typedef struct diag_ctx_s
{
	sprite_t title_sprite;
	sprite_t body_sprite;
} diag_ctx_t;

int sdiag_show(
	const char *title,
	const char *message,
	const char **buttons,
	int num_buttons,
	DialogIcon icon);