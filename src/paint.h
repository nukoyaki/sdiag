#pragma once
#include <stdint.h>
#include "types.h"

#define EMBOSS_NORMAL 0
#define EMBOSS_HOVER 1
#define EMBOSS_DOWN 2

void paint_target(sprite_t *t);
void paint_clip(rect_t r);

void paint_fill(rect_t r, pixel_t c);
void paint_blit(const sprite_t *s, int x, int y);
void paint_blit_keyed(const sprite_t *s, int x, int y);
void paint_text(const uint32_t *cp, int n, int x, int y, pixel_t fg, pixel_t bg);
void paint_text_sel(const uint32_t *cp, int n, int x, int y, int sel_lo, int sel_hi, int width);

void paint_bar_top(rect_t r);
void paint_bar_bottom(rect_t r);
void paint_emboss(rect_t r, int state);
pixel_t paint_emboss_fill(int state);
void paint_scrollbar(int x, int y, int h);
void paint_fade(rect_t band, pixel_t bg, int from_top);

void paint_present(const sprite_t *src, uint32_t *out, int ow, int oh, int scale120, const rect_t *src_dirty, rect_t *dst_dirty);
