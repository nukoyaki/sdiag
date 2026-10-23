#include <stdlib.h>
#include <string.h>
#include "atlas.h"
#include "config.h"
#include "font.h"
#include "paint.h"

static sprite_t *T;
static rect_t C;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

void paint_target(sprite_t *t)
{
	T = t;
	C = (rect_t){0, 0, t->width, t->height};
}

void paint_clip(rect_t r)
{
	C = (rect_t){imax(r.x0, 0), imax(r.y0, 0), imin(r.x1, T->width), imin(r.y1, T->height)};
}

static void put_row(int y, int x, const pixel_t *src, int n)
{
	if (y < C.y0 || y >= C.y1)
		return;
	int x0 = imax(x, C.x0), x1 = imin(x + n, C.x1);
	if (x0 >= x1)
		return;
	memcpy(&T->data[y * T->width + x0], src + (x0 - x), (size_t)(x1 - x0) * sizeof(pixel_t));
}

static void put_span(int y, int x, int n, pixel_t c)
{
	if (y < C.y0 || y >= C.y1)
		return;
	pixel_t *p = &T->data[y * T->width];
	for (int i = imax(x, C.x0); i < imin(x + n, C.x1); i++)
		p[i] = c;
}

void paint_fill(rect_t r, pixel_t c)
{
	for (int y = r.y0; y < r.y1; y++)
		put_span(y, r.x0, r.x1 - r.x0, c);
}

void paint_blit(const sprite_t *s, int x, int y)
{
	int y0 = imax(y, C.y0), y1 = imin(y + s->height, C.y1);
	for (int yy = y0; yy < y1; yy++)
		put_row(yy, x, &s->data[(yy - y) * s->width], s->width);
}

void paint_blit_keyed(const sprite_t *s, int x, int y)
{
	pixel_t key = s->data[0];
	int y0 = imax(y, C.y0), y1 = imin(y + s->height, C.y1);
	for (int yy = y0; yy < y1; yy++)
	{
		const pixel_t *row = &s->data[(yy - y) * s->width];
		pixel_t *dst = &T->data[yy * T->width];
		for (int i = imax(x, C.x0); i < imin(x + s->width, C.x1); i++)
		{
			pixel_t p = row[i - x];
			if (p.R != key.R || p.G != key.G || p.B != key.B)
				dst[i] = p;
		}
	}
}

static void cell(int x, int y, uint32_t cp, pixel_t fg, pixel_t bg)
{
	const uint8_t *g = font_glyph(cp);
	for (int r = 0; r < FONT_H; r++)
	{
		int yy = y + r;
		if (yy < C.y0 || yy >= C.y1)
			continue;
		pixel_t *dst = &T->data[yy * T->width];
		for (int c = 0, bit = r * FONT_W; c < FONT_W; c++, bit++)
		{
			int xx = x + c;
			if (xx < C.x0 || xx >= C.x1)
				continue;
			dst[xx] = g && ((g[bit >> 3] >> (7 - (bit & 7))) & 1) ? fg : bg;
		}
	}
}

void paint_text(const uint32_t *cp, int n, int x, int y, pixel_t fg, pixel_t bg)
{
	for (int i = 0; i < n; i++)
		cell(x + i * FONT_W, y, cp[i], fg, bg);
}

void paint_text_sel(const uint32_t *cp, int n, int x, int y, int sel_lo, int sel_hi, int width)
{
	for (int r = 0; r < FONT_H; r++)
		put_span(y + r, x, width, g_bg_color);
	for (int i = 0; i < n; i++)
	{
		int sel = i >= sel_lo && i < sel_hi;
		cell(x + i * FONT_W, y, cp[i], sel ? g_text_sel_color : g_text_color, sel ? g_bg_sel_color : g_bg_color);
	}
}

void paint_bar_top(rect_t r)
{
	const sprite_t *s = &g_spr_section_top;
	int w = r.x1 - r.x0, n = imin(w, s->width);
	for (int y = r.y0; y < r.y1; y++)
	{
		const pixel_t *row = &s->data[imax(y - (r.y1 - s->height), 0) * s->width];
		put_row(y, r.x0, row, n);
		if (w > n)
			put_span(y, r.x0 + n, w - n, row[s->width - 1]);
	}
}

void paint_bar_bottom(rect_t r)
{
	const sprite_t *s = &g_spr_section_bottom;
	int w = r.x1 - r.x0, n = imin(w, s->width);
	for (int y = r.y0; y < r.y1; y++)
	{
		const pixel_t *row = &s->data[imin(y - r.y0, s->height - 1) * s->width];
		put_row(y, r.x0, row, n);
		if (w > n)
			put_span(y, r.x0 + n, w - n, row[s->width - 1]);
	}
}

static const sprite_t *emboss_set[3][4] = {
	{&g_spr_emboss_tl, &g_spr_emboss_tr, &g_spr_emboss_bl, &g_spr_emboss_br},
	{&g_spr_emboss_hov_tl, &g_spr_emboss_hov_tr, &g_spr_emboss_hov_bl, &g_spr_emboss_hov_br},
	{&g_spr_emboss_down_tl, &g_spr_emboss_down_tr, &g_spr_emboss_down_bl, &g_spr_emboss_down_br},
};

pixel_t paint_emboss_fill(int state)
{
	const sprite_t *tl = emboss_set[state][0];
	return tl->data[(tl->height - 1) * tl->width + tl->width - 1];
}

void paint_emboss(rect_t r, int state)
{
	const sprite_t *tl = emboss_set[state][0], *tr = emboss_set[state][1];
	const sprite_t *bl = emboss_set[state][2], *br = emboss_set[state][3];
	int w = r.x1 - r.x0, h = r.y1 - r.y0, cw = tl->width, ch = tl->height;
	for (int i = 0; i < h; i++)
	{
		const sprite_t *l = tl, *rr = tr;
		int sr = ch - 1;
		if (i < ch)
			sr = i;
		else if (i >= h - ch)
		{
			l = bl;
			rr = br;
			sr = i - (h - ch);
		}
		const pixel_t *lrow = &l->data[sr * l->width], *rrow = &rr->data[sr * rr->width];
		put_row(r.y0 + i, r.x0, lrow, cw);
		put_span(r.y0 + i, r.x0 + cw, w - 2 * cw, lrow[cw - 1]);
		put_row(r.y0 + i, r.x1 - cw, rrow, cw);
	}
}

void paint_scrollbar(int x, int y, int h)
{
	const sprite_t *a = &g_spr_section_scroll_piece_start, *m = &g_spr_section_scroll_piece_middle, *e = &g_spr_section_scroll_piece_end;
	for (int i = 0; i < a->height; i++)
		put_row(y + i, x, &a->data[i * a->width], a->width);
	for (int i = 0, n = h - a->height - e->height; i < n; i++)
		put_row(y + a->height + i, x, &m->data[(i % m->height) * m->width], m->width);
	for (int i = 0; i < e->height; i++)
		put_row(y + h - e->height + i, x, &e->data[i * e->width], e->width);
}

void paint_fade(rect_t band, pixel_t bg, int from_top)
{
	int n = band.y1 - band.y0;
	for (int y = imax(band.y0, C.y0); y < imin(band.y1, C.y1); y++)
	{
		int k = from_top ? band.y1 - 1 - y : y - band.y0, a = imin((k + 1) * 256 / n, 256);
		pixel_t *row = &T->data[y * T->width];
		for (int x = imax(band.x0, C.x0); x < imin(band.x1, C.x1); x++)
		{
			row[x].R = (uint8_t)(row[x].R + ((bg.R - row[x].R) * a) / 256);
			row[x].G = (uint8_t)(row[x].G + ((bg.G - row[x].G) * a) / 256);
			row[x].B = (uint8_t)(row[x].B + ((bg.B - row[x].B) * a) / 256);
		}
	}
}

void paint_present(const sprite_t *src, uint32_t *out, int ow, int oh, int S, const rect_t *sd, rect_t *dd)
{
	rect_t r = sd ? *sd : (rect_t){0, 0, src->width, src->height};
	rect_t o = {imax((r.x0 * S + 119) / 120, 0), imax((r.y0 * S + 119) / 120, 0),
				imin((r.x1 * S + 119) / 120, ow), imin((r.y1 * S + 119) / 120, oh)};
	*dd = o;
	if (o.x0 >= o.x1 || o.y0 >= o.y1)
		return;

	int *xmap = malloc((size_t)(o.x1 - o.x0) * sizeof *xmap);
	if (!xmap)
		return;
	for (int x = o.x0; x < o.x1; x++)
		xmap[x - o.x0] = imin(x * 120 / S, src->width - 1);

	for (int y = o.y0; y < o.y1; y++)
	{
		const pixel_t *row = &src->data[imin(y * 120 / S, src->height - 1) * src->width];
		uint32_t *dst = &out[(size_t)y * ow];
		for (int x = o.x0; x < o.x1; x++)
		{
			pixel_t p = row[xmap[x - o.x0]];
			dst[x] = (uint32_t)p.R << 16 | (uint32_t)p.G << 8 | p.B;
		}
	}
	free(xmap);
}
