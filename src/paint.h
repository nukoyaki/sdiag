#pragma once
#include <stdint.h>
#include "types.h"

#define EMBOSS_NORMAL 0
#define EMBOSS_HOVER 1
#define EMBOSS_DOWN 2

typedef struct paint_s
{
	sprite_t *t;
	rect_t c;
} paint_t;

void paint_target(paint_t *pc, sprite_t *t);
void paint_clip(paint_t *pc, rect_t r);

void paint_fill(paint_t *pc, rect_t r, pixel_t c);
void paint_blit(paint_t *pc, const sprite_t *s, int x, int y);
void paint_blit_keyed(paint_t *pc, const sprite_t *s, int x, int y);
void paint_text(paint_t *pc, const uint32_t *cp, int n, int x, int y, pixel_t fg, pixel_t bg);
void paint_text_sel(paint_t *pc, const uint32_t *cp, int n, int x, int y, int sel_lo, int sel_hi, int width);

void paint_bar_top(paint_t *pc, rect_t r);
void paint_bar_bottom(paint_t *pc, rect_t r);
void paint_emboss(paint_t *pc, rect_t r, int state);
pixel_t paint_emboss_fill(int state);
void paint_scrollbar(paint_t *pc, int x, int y, int h);
void paint_fade(paint_t *pc, rect_t band, pixel_t bg, int from_top);

void paint_present(const sprite_t *src, uint32_t *out, int ow, int oh, int scale120, const rect_t *src_dirty, rect_t *dst_dirty);
