#include <stdlib.h>
#include <string.h>
#include "atlas.h"
#include "paint.h"
#include "ui.h"

enum
{
	T_NONE,
	T_BTN,
	T_CLOSE,
	T_TEXT,
	T_THUMB
};

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int inr(rect_t r, int x, int y) { return x >= r.x0 && y >= r.y0 && x < r.x1 && y < r.y1; }
static int empty(rect_t r) { return r.x0 >= r.x1 || r.y0 >= r.y1; }
static int isect(rect_t a, rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static rect_t clipto(rect_t a, rect_t b) { return (rect_t){imax(a.x0, b.x0), imax(a.y0, b.y0), imin(a.x1, b.x1), imin(a.y1, b.y1)}; }

static void mark(diag_ctx_t *ctx, rect_t r)
{
	if (empty(r))
		return;
	if (empty(ctx->dirty))
		ctx->dirty = r;
	else
		ctx->dirty = (rect_t){imin(ctx->dirty.x0, r.x0), imin(ctx->dirty.y0, r.y0), imax(ctx->dirty.x1, r.x1), imax(ctx->dirty.y1, r.y1)};
}

static rect_t title_r(diag_ctx_t *ctx) { return (rect_t){0, 0, ctx->W, TITLE_H}; }
static rect_t body_r(diag_ctx_t *ctx) { return (rect_t){0, TITLE_H, ctx->W, ctx->H - BOTTOM_H}; }
static rect_t bottom_r(diag_ctx_t *ctx) { return (rect_t){0, ctx->H - BOTTOM_H, ctx->W, ctx->H}; }

static uint32_t u8next(const char **pp)
{
	const unsigned char *p = (const unsigned char *)*pp;
	uint32_t c = *p++;
	int n = 0;
	if (c >= 0x80)
	{
		if ((c & 0xE0) == 0xC0)
		{
			c &= 0x1F;
			n = 1;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			c &= 0x0F;
			n = 2;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			c &= 0x07;
			n = 3;
		}
		else
			c = 0xFFFD;
	}
	while (n--)
	{
		if ((*p & 0xC0) != 0x80)
		{
			c = 0xFFFD;
			break;
		}
		c = (c << 6) | (*p++ & 0x3F);
	}
	*pp = (const char *)p;
	return c;
}

static char *u8put(char *o, uint32_t c)
{
	if (c < 0x80)
		*o++ = (char)c;
	else if (c < 0x800)
	{
		*o++ = (char)(0xC0 | c >> 6);
		*o++ = (char)(0x80 | (c & 0x3F));
	}
	else if (c < 0x10000)
	{
		*o++ = (char)(0xE0 | c >> 12);
		*o++ = (char)(0x80 | (c >> 6 & 0x3F));
		*o++ = (char)(0x80 | (c & 0x3F));
	}
	else
	{
		*o++ = (char)(0xF0 | c >> 18);
		*o++ = (char)(0x80 | (c >> 12 & 0x3F));
		*o++ = (char)(0x80 | (c >> 6 & 0x3F));
		*o++ = (char)(0x80 | (c & 0x3F));
	}
	return o;
}

static uint32_t *decode(const char *s, int *n)
{
	size_t len = strlen(s);
	uint32_t *o = malloc((len + 1) * sizeof *o);
	int k = 0;
	if (o)
		while (*s)
			o[k++] = u8next(&s);
	*n = k;
	return o;
}

static int btn_w(diag_ctx_t *ctx, int i) { return imax(BTN_MIN_W, 2 * BTN_PAD_X + ctx->bcells[i] * FONT_W); }

static int btn_row(diag_ctx_t *ctx)
{
	int w = 2 * BTN_MARGIN;
	for (int i = 0; i < ctx->nbtn; i++)
		w += btn_w(ctx, i) + (i ? BTN_GAP : 0);
	return w;
}

static void wrap(diag_ctx_t *ctx, int maxcols)
{
	const uint32_t *c = ctx->cps;
	int i = 0, n = ctx->ncps;
	ctx->nlines = 0;
	ctx->nshown = n;
	do
	{
		if (ctx->nlines == MAX_LINES)
		{
			ctx->nshown = i;
			break;
		}
		int start = i, col = 0, brk = start;
		while (i < n && c[i] != '\n')
		{
			if (col >= maxcols)
			{
				if (c[i] == ' ')
					i++;
				else if (brk > start)
					i = brk;
				break;
			}
			if (c[i] == ' ')
				brk = i + 1;
			i++;
			col++;
		}
		line_t ln = {start, i, 0};
		if (i < n && c[i] == '\n')
		{
			ln.nl = 1;
			i++;
		}
		ctx->lines[ctx->nlines++] = ln;
	} while (i < n);
}

static int sel_lo(diag_ctx_t *ctx) { return imin(ctx->sel_a, ctx->sel_b); }
static int sel_hi(diag_ctx_t *ctx) { return imax(ctx->sel_a, ctx->sel_b); }

static int line_of(diag_ctx_t *ctx, int idx)
{
	int i = ctx->nlines - 1;
	while (i > 0 && ctx->lines[i].start > idx)
		i--;
	return i;
}

static rect_t line_rect(diag_ctx_t *ctx, int i)
{
	int y = TITLE_H + TEXT_Y + i * LINE_H - ctx->scroll;
	return clipto((rect_t){TEXT_X, y, TEXT_X + ctx->body_sprite.width, y + LINE_H}, body_r(ctx));
}

static void render_line(diag_ctx_t *ctx, int i)
{
	const line_t *ln = &ctx->lines[i];
	int lo = sel_lo(ctx), hi = sel_hi(ctx), n = ln->end - ln->start;
	paint_target(&ctx->paint, &ctx->body_sprite);
	paint_text_sel(&ctx->paint, ctx->cps + ln->start, n, 0, i * LINE_H, lo - ln->start, hi - ln->start, ctx->body_sprite.width);
	if (ln->nl && ln->end >= lo && ln->end < hi && n < ctx->cols)
		paint_fill(&ctx->paint, (rect_t){n * FONT_W, i * LINE_H, (n + 1) * FONT_W, (i + 1) * LINE_H}, g_bg_sel_color);
}

static void set_sel(diag_ctx_t *ctx, int a, int b)
{
	int olo = sel_lo(ctx), ohi = sel_hi(ctx);
	ctx->sel_a = a;
	ctx->sel_b = b;
	if (olo == sel_lo(ctx) && ohi == sel_hi(ctx))
		return;
	int l0 = line_of(ctx, imin(olo, sel_lo(ctx))), l1 = line_of(ctx, imax(ohi, sel_hi(ctx)));
	for (int i = l0; i <= l1; i++)
	{
		render_line(ctx, i);
		mark(ctx, line_rect(ctx, i));
	}
}

static void layout_buttons(diag_ctx_t *ctx)
{
	int r = ctx->W - BTN_MARGIN, y = ctx->H - BOTTOM_H + BTN_Y;
	for (int i = 0; i < ctx->nbtn; i++)
	{
		int w = btn_w(ctx, i);
		ctx->btn_r[i] = (rect_t){r - w, y, r, y + BTN_H};
		r -= w + BTN_GAP;
	}
}

static void layout(diag_ctx_t *ctx)
{
	ctx->vh = ctx->H - TITLE_H - BOTTOM_H;
	ctx->total = imax(TEXT_Y + ctx->nlines * LINE_H, ICON_Y + g_spr_info.height) + TEXT_PAD_BOTTOM;
	ctx->maxscroll = imax(ctx->total - ctx->vh, 0);
	ctx->sbar = ctx->total > ctx->vh;
	if (ctx->scroll > ctx->maxscroll)
		ctx->scroll = ctx->maxscroll;

	int trk = ctx->vh - 2 * SCROLL_TRACK_PAD;
	int fixed = g_spr_section_scroll_piece_start.height + g_spr_section_scroll_piece_end.height;
	int minth = fixed + g_spr_section_scroll_piece_middle.height;
	int th = ctx->total ? trk * ctx->vh / ctx->total : trk;
	th = fixed + (imin(th, trk) - fixed) / 4 * 4;
	th = imax(imin(th, fixed + (trk - fixed) / 4 * 4), minth);
	int ty = TITLE_H + SCROLL_TRACK_PAD + (ctx->maxscroll ? (trk - th) * ctx->scroll / ctx->maxscroll : 0);
	ctx->track_r = (rect_t){ctx->W - SCROLL_W, TITLE_H + SCROLL_TRACK_PAD, ctx->W, TITLE_H + SCROLL_TRACK_PAD + trk};
	ctx->thumb_r = (rect_t){ctx->W - SCROLL_W, ty, ctx->W, ty + th};
	ctx->close_r = (rect_t){ctx->W - CLOSE_MARGIN - CLOSE_BOX, CLOSE_MARGIN, ctx->W - CLOSE_MARGIN, CLOSE_MARGIN + CLOSE_BOX};
	layout_buttons(ctx);
}

static void set_scroll(diag_ctx_t *ctx, int s)
{
	s = imax(imin(s, ctx->maxscroll), 0);
	if (s == ctx->scroll)
		return;
	ctx->scroll = s;
	layout(ctx);
	mark(ctx, body_r(ctx));
}

static int alloc_screen(diag_ctx_t *ctx)
{
	free(ctx->screen.data);
	ctx->screen.width = (unsigned short)ctx->W;
	ctx->screen.height = (unsigned short)ctx->H;
	ctx->screen.data = malloc((size_t)ctx->W * ctx->H * sizeof(pixel_t));
	return ctx->screen.data != NULL;
}

static int make_title_sprite(diag_ctx_t *ctx)
{
	int avail = ctx->W - TITLE_TEXT_X - CLOSE_MARGIN - CLOSE_BOX - TITLE_TEXT_GAP;
	int cols = imax(avail / FONT_W, 0), n;
	uint32_t *all = decode(ctx->title, &n);
	if (!all)
		return 0;
	uint32_t shown[256];
	int k = 0;
	if (n <= cols)
	{
		for (; k < n && k < 256; k++)
			shown[k] = all[k];
	}
	else
	{
		int dots = imin(cols, 3), keep = cols - dots;
		for (int i = 0; i < keep && k < 253; i++)
			shown[k++] = all[i];
		for (int i = 0; i < dots; i++)
			shown[k++] = '.';
	}
	free(all);
	int w = imax(k, 1) * FONT_W;
	ctx->title_sprite.width = (unsigned short)w;
	ctx->title_sprite.height = FONT_H;
	ctx->title_sprite.data = malloc((size_t)w * FONT_H * sizeof(pixel_t));
	if (!ctx->title_sprite.data)
		return 0;
	paint_target(&ctx->paint, &ctx->title_sprite);
	paint_fill(&ctx->paint, (rect_t){0, 0, w, FONT_H}, g_bg2_color);
	paint_text(&ctx->paint, shown, k, 0, 0, g_text_color, g_bg2_color);
	return 1;
}

int ui_init(diag_ctx_t *ctx, const char *title, const char *message, const char *const *buttons, int n, int icon)
{
	memset(ctx, 0, sizeof *ctx);
	if (n > MAX_BUTTONS)
		n = MAX_BUTTONS;
	ctx->title = title ? title : "";
	ctx->message = message;
	ctx->btn = buttons;
	ctx->nbtn = n;
	ctx->icon = icon < 0 || icon > 2 ? 0 : icon;
	ctx->hover_btn = -1;
	ctx->result = -1;
	ctx->mx = ctx->my = -1;

	for (int i = 0; i < n; i++)
	{
		ctx->bcp[i] = decode(buttons[i], &ctx->bcells[i]);
		if (!ctx->bcp[i])
		{
			ui_free(ctx);
			return 0;
		}
	}
	int tn;
	uint32_t *t = decode(ctx->title, &tn);
	if (!t)
	{
		ui_free(ctx);
		return 0;
	}
	free(t);
	ctx->tcells = tn;

	size_t ml = strlen(message);
	ctx->cps = malloc((ml * TAB_COLS + 1) * sizeof *ctx->cps);
	ctx->lines = malloc((ml * TAB_COLS + 2) * sizeof *ctx->lines);
	if (!ctx->cps || !ctx->lines)
	{
		ui_free(ctx);
		return 0;
	}

	int col = 0;
	for (const char *p = message; *p;)
	{
		uint32_t c = u8next(&p);
		if (c == '\r')
			continue;
		if (c == '\t')
		{
			do
			{
				ctx->cps[ctx->ncps++] = ' ';
				col++;
			} while (col % TAB_COLS);
		}
		else if (c == '\n')
		{
			ctx->cps[ctx->ncps++] = c;
			col = 0;
		}
		else
		{
			ctx->cps[ctx->ncps++] = c;
			col++;
		}
		if (col > ctx->maxcells)
			ctx->maxcells = col;
	}

	int minw = imax(WIN_MIN_W, btn_row(ctx)), maxw = imax(WIN_MAX_W, minw);
	int want = imax(TEXT_X + TEXT_RIGHT_MARGIN + ctx->maxcells * FONT_W,
					TITLE_TEXT_X + ctx->tcells * FONT_W + TITLE_TEXT_GAP + CLOSE_BOX + CLOSE_MARGIN);
	ctx->W = imax(imin(want, maxw), minw);
	ctx->cols = imax((ctx->W - TEXT_X - TEXT_RIGHT_MARGIN) / FONT_W, 1);

	wrap(ctx, ctx->cols);
	int content = imax(TEXT_Y + ctx->nlines * LINE_H, ICON_Y + g_spr_info.height) + TEXT_PAD_BOTTOM;
	ctx->H = imax(imin(TITLE_H + content + BOTTOM_H, INIT_MAX_H), WIN_MIN_H);

	ctx->body_sprite.width = (unsigned short)(ctx->cols * FONT_W);
	ctx->body_sprite.height = (unsigned short)(ctx->nlines * LINE_H);
	ctx->body_sprite.data = malloc((size_t)ctx->body_sprite.width * ctx->body_sprite.height * sizeof(pixel_t));
	if (!ctx->body_sprite.data || !alloc_screen(ctx) || !make_title_sprite(ctx))
	{
		ui_free(ctx);
		return 0;
	}
	for (int i = 0; i < ctx->nlines; i++)
		render_line(ctx, i);

	layout(ctx);
	ui_invalidate(ctx);
	return 1;
}

void ui_free(diag_ctx_t *ctx)
{
	for (int i = 0; i < MAX_BUTTONS; i++)
		free(ctx->bcp[i]);
	free(ctx->cps);
	free(ctx->lines);
	free(ctx->screen.data);
	free(ctx->title_sprite.data);
	free(ctx->body_sprite.data);
	free(ctx->copybuf);
	memset(ctx, 0, sizeof *ctx);
}

const char *ui_title(diag_ctx_t *ctx) { return ctx->title; }

void ui_size(diag_ctx_t *ctx, int *w, int *h)
{
	*w = ctx->W;
	*h = ctx->H;
}

void ui_limits(int *minh, int *maxh)
{
	*minh = WIN_MIN_H;
	*maxh = imax(WIN_MAX_H, WIN_MIN_H);
}

void ui_set_height(diag_ctx_t *ctx, int h)
{
	h = imax(imin(h, WIN_MAX_H), WIN_MIN_H);
	if (h == ctx->H)
		return;
	ctx->H = h;
	if (!alloc_screen(ctx))
	{
		ctx->done = 1;
		return;
	}
	layout(ctx);
	ui_invalidate(ctx);
}

void ui_invalidate(diag_ctx_t *ctx)
{
	ctx->dirty = (rect_t){0, 0, ctx->W, ctx->H};
}

static const sprite_t *icon_sprite(diag_ctx_t *ctx)
{
	return ctx->icon == 1 ? &g_spr_warning : ctx->icon == 2 ? &g_spr_error
															: &g_spr_info;
}

static void paint_title(diag_ctx_t *ctx, rect_t d)
{
	rect_t t = title_r(ctx);
	paint_clip(&ctx->paint, clipto(d, t));
	paint_bar_top(&ctx->paint, t);
	paint_blit(&ctx->paint, &ctx->title_sprite, TITLE_TEXT_X, TITLE_TEXT_Y);
	int st = ctx->hover_close ? (ctx->down == T_CLOSE ? EMBOSS_DOWN : EMBOSS_HOVER) : EMBOSS_NORMAL;
	paint_emboss(&ctx->paint, ctx->close_r, st);
	paint_blit_keyed(&ctx->paint, &g_spr_close, ctx->close_r.x0 + CLOSE_SPRITE_INSET, ctx->close_r.y0 + CLOSE_SPRITE_INSET);
}

static void paint_body(diag_ctx_t *ctx, rect_t d)
{
	rect_t b = body_r(ctx);
	paint_clip(&ctx->paint, clipto(d, b));
	paint_fill(&ctx->paint, b, g_bg_color);
	paint_blit(&ctx->paint, icon_sprite(ctx), ICON_X, TITLE_H + ICON_Y - ctx->scroll);
	paint_blit(&ctx->paint, &ctx->body_sprite, TEXT_X, TITLE_H + TEXT_Y - ctx->scroll);
	if (ctx->scroll > 0)
		paint_fade(&ctx->paint, (rect_t){0, b.y0, ctx->W, b.y0 + FADE_H}, g_bg_color, 1);
	if (ctx->scroll < ctx->maxscroll)
		paint_fade(&ctx->paint, (rect_t){0, b.y1 - FADE_H, ctx->W, b.y1}, g_bg_color, 0);
	if (ctx->sbar)
		paint_scrollbar(&ctx->paint, ctx->thumb_r.x0, ctx->thumb_r.y0, ctx->thumb_r.y1 - ctx->thumb_r.y0);
}

static void paint_bottom(diag_ctx_t *ctx, rect_t d)
{
	rect_t b = bottom_r(ctx);
	paint_clip(&ctx->paint, clipto(d, b));
	paint_bar_bottom(&ctx->paint, b);
	for (int i = 0; i < ctx->nbtn; i++)
	{
		rect_t r = ctx->btn_r[i];
		int pressed = ctx->down == T_BTN && ctx->down_i == i && ctx->hover_btn == i;
		int st = pressed ? EMBOSS_DOWN : ctx->focus == i ? EMBOSS_HOVER
														 : EMBOSS_NORMAL;
		paint_emboss(&ctx->paint, r, st);
		paint_text(&ctx->paint, ctx->bcp[i], ctx->bcells[i], r.x0 + (r.x1 - r.x0 - ctx->bcells[i] * FONT_W) / 2, r.y0 + BTN_TEXT_Y,
				   g_text_color, paint_emboss_fill(st));
	}
}

int ui_paint(diag_ctx_t *ctx, rect_t *out)
{
	if (empty(ctx->dirty))
		return 0;
	rect_t d = clipto(ctx->dirty, (rect_t){0, 0, ctx->W, ctx->H});
	ctx->dirty = (rect_t){0, 0, 0, 0};
	if (empty(d))
		return 0;
	paint_target(&ctx->paint, &ctx->screen);
	if (isect(d, title_r(ctx)))
		paint_title(ctx, d);
	if (isect(d, body_r(ctx)))
		paint_body(ctx, d);
	if (isect(d, bottom_r(ctx)))
		paint_bottom(ctx, d);
	*out = d;
	return 1;
}

void ui_present(diag_ctx_t *ctx, uint32_t *out, int ow, int oh, int S, const rect_t *sd, rect_t *dd)
{
	paint_present(&ctx->screen, out, ow, oh, S, sd, dd);
}

static int hit_btn(diag_ctx_t *ctx, int x, int y)
{
	for (int i = 0; i < ctx->nbtn; i++)
		if (inr(ctx->btn_r[i], x, y))
			return i;
	return -1;
}

static int hit_edge(diag_ctx_t *ctx, int x, int y)
{
	(void)x;
	if (y >= 0 && y < RESIZE_ZONE)
		return EDGE_T;
	if (y >= ctx->H - RESIZE_ZONE && y < ctx->H)
		return EDGE_B;
	return 0;
}

static int in_text_area(diag_ctx_t *ctx, int x, int y)
{
	return y >= TITLE_H && y < ctx->H - BOTTOM_H && x >= 0 && x < ctx->W - SCROLL_W;
}

static int line_at(diag_ctx_t *ctx, int y)
{
	int v = y - (TITLE_H + TEXT_Y) + ctx->scroll, li = v < 0 ? 0 : v / LINE_H;
	return li >= ctx->nlines ? ctx->nlines - 1 : li;
}

static int idx_at(diag_ctx_t *ctx, int x, int y, int caret)
{
	const line_t *ln = &ctx->lines[line_at(ctx, y)];
	int len = ln->end - ln->start, hx = x - TEXT_X + (caret ? FONT_W / 2 : 0);
	int col = hx < 0 ? 0 : hx / FONT_W, max = caret ? len : len - 1;
	if (col > max)
		col = max;
	if (col < 0)
		col = 0;
	return ln->start + col;
}

static int cls(uint32_t c)
{
	if (c == ' ')
		return 0;
	if (c == '_' || c >= 128 || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z'))
		return 1;
	return 2;
}

static void select_word(diag_ctx_t *ctx, int x, int y)
{
	const line_t *ln = &ctx->lines[line_at(ctx, y)];
	if (ln->end == ln->start)
		return;
	int c = idx_at(ctx, x, y, 0), k = cls(ctx->cps[c]), lo = c, hi = c + 1;
	while (lo > ln->start && cls(ctx->cps[lo - 1]) == k)
		lo--;
	while (hi < ln->end && cls(ctx->cps[hi]) == k)
		hi++;
	set_sel(ctx, lo, hi);
}

static void finish(diag_ctx_t *ctx, int r)
{
	ctx->result = r;
	ctx->done = 1;
}

static void set_focus(diag_ctx_t *ctx, int i)
{
	if (i == ctx->focus)
		return;
	mark(ctx, ctx->btn_r[ctx->focus]);
	ctx->focus = i;
	mark(ctx, ctx->btn_r[i]);
}

void ui_pointer(diag_ctx_t *ctx, int x, int y)
{
	ctx->mx = x;
	ctx->my = y;
	int hb = hit_btn(ctx, x, y), hc = inr(ctx->close_r, x, y);
	if (hc != ctx->hover_close)
	{
		ctx->hover_close = hc;
		mark(ctx, ctx->close_r);
	}
	if (hb != ctx->hover_btn)
	{
		if (ctx->hover_btn >= 0)
			mark(ctx, ctx->btn_r[ctx->hover_btn]);
		ctx->hover_btn = hb;
		if (hb >= 0)
			mark(ctx, ctx->btn_r[hb]);
	}
	if (hb >= 0)
		set_focus(ctx, hb);

	if (ctx->down == T_TEXT)
	{
		if (y < TITLE_H)
			set_scroll(ctx, ctx->scroll - LINE_H);
		else if (y >= ctx->H - BOTTOM_H)
			set_scroll(ctx, ctx->scroll + LINE_H);
		set_sel(ctx, ctx->sel_a, idx_at(ctx, x, y, 1));
	}
	else if (ctx->down == T_THUMB)
	{
		int range = ctx->track_r.y1 - ctx->track_r.y0 - (ctx->thumb_r.y1 - ctx->thumb_r.y0);
		if (range > 0)
			set_scroll(ctx, (y - ctx->dragoff - ctx->track_r.y0) * ctx->maxscroll / range);
	}
}

void ui_leave(diag_ctx_t *ctx)
{
	ctx->mx = ctx->my = -1;
	if (ctx->hover_close)
	{
		ctx->hover_close = 0;
		mark(ctx, ctx->close_r);
	}
	if (ctx->hover_btn >= 0)
	{
		mark(ctx, ctx->btn_r[ctx->hover_btn]);
		ctx->hover_btn = -1;
	}
}

void ui_button(diag_ctx_t *ctx, int pressed, unsigned time)
{
	int x = ctx->mx, y = ctx->my;
	if (!pressed)
	{
		if (ctx->down == T_CLOSE && inr(ctx->close_r, x, y))
			finish(ctx, -1);
		else if (ctx->down == T_BTN && hit_btn(ctx, x, y) == ctx->down_i)
			finish(ctx, ctx->down_i);
		if (ctx->down == T_CLOSE)
			mark(ctx, ctx->close_r);
		if (ctx->down == T_BTN)
			mark(ctx, ctx->btn_r[ctx->down_i]);
		ctx->down = T_NONE;
		return;
	}
	int b, e;
	if (inr(ctx->close_r, x, y))
	{
		ctx->down = T_CLOSE;
		mark(ctx, ctx->close_r);
	}
	else if ((e = hit_edge(ctx, x, y)))
	{
		ctx->action = ACT_RESIZE;
		ctx->edge = e;
	}
	else if ((b = hit_btn(ctx, x, y)) >= 0)
	{
		ctx->down = T_BTN;
		ctx->down_i = b;
		set_focus(ctx, b);
		mark(ctx, ctx->btn_r[b]);
	}
	else if (y < TITLE_H)
		ctx->action = ACT_MOVE;
	else if (ctx->sbar && inr(ctx->track_r, x, y))
	{
		if (inr(ctx->thumb_r, x, y))
		{
			ctx->down = T_THUMB;
			ctx->dragoff = y - ctx->thumb_r.y0;
		}
		else
			set_scroll(ctx, ctx->scroll + (y < ctx->thumb_r.y0 ? -1 : 1) * (ctx->vh * 9 / 10));
	}
	else if (in_text_area(ctx, x, y))
	{
		int dbl = ctx->last_valid && time - ctx->last_t < DOUBLE_CLICK_MS && abs(x - ctx->last_x) <= 4 && abs(y - ctx->last_y) <= 4;
		ctx->last_valid = !dbl;
		ctx->last_t = time;
		ctx->last_x = x;
		ctx->last_y = y;
		if (dbl)
			select_word(ctx, x, y);
		else
		{
			int i = idx_at(ctx, x, y, 1);
			ctx->down = T_TEXT;
			set_sel(ctx, i, i);
		}
	}
}

void ui_wheel(diag_ctx_t *ctx, int lines_x100)
{
	set_scroll(ctx, ctx->scroll + lines_x100 * LINE_H / 100);
}

static void do_copy(diag_ctx_t *ctx)
{
	free(ctx->copybuf);
	ctx->copybuf = NULL;
	ctx->copylen = 0;
	int lo = sel_lo(ctx), hi = sel_hi(ctx);
	char *o;
	if (lo != hi)
	{
		o = ctx->copybuf = malloc((size_t)(hi - lo) * 4 + 1);
		if (!o)
			return;
		for (int i = lo; i < hi; i++)
			o = u8put(o, ctx->cps[i]);
	}
	else
	{
		size_t cap = strlen(ctx->title) + strlen(ctx->message) + 128;
		for (int i = 0; i < ctx->nbtn; i++)
			cap += strlen(ctx->btn[i]) + 8;
		o = ctx->copybuf = malloc(cap);
		if (!o)
			return;
#define APP(s)                 \
	do                         \
	{                          \
		size_t l_ = strlen(s); \
		memcpy(o, s, l_);      \
		o += l_;               \
	} while (0)
		APP("--- ");
		APP(ctx->title);
		APP(" ---\n\n");
		APP(ctx->message);
		APP("\n------------------------------------------------\n\n{ ");
		for (int i = ctx->nbtn - 1; i >= 0; i--)
		{
			APP(ctx->btn[i]);
			if (i)
				APP(" } { ");
		}
		APP(" }");
#undef APP
	}
	ctx->copylen = (size_t)(o - ctx->copybuf);
	ctx->copy = 1;
}

void ui_key(diag_ctx_t *ctx, int k, int down)
{
	switch (k)
	{
	case 29:
	case 97:
		ctx->ctrl = down;
		return;
	case 42:
	case 54:
		ctx->shift = down;
		return;
	}
	if (!down)
		return;
	switch (k)
	{
	case 1:
		finish(ctx, -1);
		break;
	case 28:
	case 96:
	case 57:
		finish(ctx, ctx->focus);
		break;
	case 105:
		if (ctx->focus < ctx->nbtn - 1)
			set_focus(ctx, ctx->focus + 1);
		break;
	case 106:
		if (ctx->focus > 0)
			set_focus(ctx, ctx->focus - 1);
		break;
	case 15:
		set_focus(ctx, ctx->shift ? (ctx->focus + ctx->nbtn - 1) % ctx->nbtn : (ctx->focus + 1) % ctx->nbtn);
		break;
	case 30:
		if (ctx->ctrl)
			set_sel(ctx, 0, ctx->nshown);
		break;
	case 46:
		if (ctx->ctrl)
			do_copy(ctx);
		break;
	case 103:
		set_scroll(ctx, ctx->scroll - LINE_H);
		break;
	case 108:
		set_scroll(ctx, ctx->scroll + LINE_H);
		break;
	case 104:
		set_scroll(ctx, ctx->scroll - ctx->vh * 9 / 10);
		break;
	case 109:
		set_scroll(ctx, ctx->scroll + ctx->vh * 9 / 10);
		break;
	case 102:
		set_scroll(ctx, 0);
		break;
	case 107:
		set_scroll(ctx, ctx->maxscroll);
		break;
	}
}

void ui_focus_lost(diag_ctx_t *ctx)
{
	ctx->ctrl = ctx->shift = 0;
}

int ui_done(diag_ctx_t *ctx) { return ctx->done; }
int ui_result(diag_ctx_t *ctx) { return ctx->result; }

int ui_take_action(diag_ctx_t *ctx, int *edge)
{
	int a = ctx->action;
	*edge = ctx->edge;
	ctx->action = ACT_NONE;
	return a;
}

const char *ui_take_copy(diag_ctx_t *ctx, size_t *len)
{
	if (!ctx->copy)
		return NULL;
	ctx->copy = 0;
	*len = ctx->copylen;
	return ctx->copybuf;
}

int ui_cursor(diag_ctx_t *ctx)
{
	if (ctx->down == T_TEXT)
		return CUR_TEXT;
	if (ctx->down != T_NONE || inr(ctx->close_r, ctx->mx, ctx->my) || hit_btn(ctx, ctx->mx, ctx->my) >= 0)
		return CUR_ARROW;
	int e = hit_edge(ctx, ctx->mx, ctx->my);
	if (e)
		return e == EDGE_T ? CUR_RESIZE_T : CUR_RESIZE_B;
	return in_text_area(ctx, ctx->mx, ctx->my) ? CUR_TEXT : CUR_ARROW;
}

static int scale_from(const char *e)
{
	if (!e)
		return 0;
	double v = strtod(e, NULL);
	return v > 0 ? (int)(v * 120 + 0.5) : 0;
}

int ui_scale_override(void) { return scale_from(getenv("SDIAG_SCALE")); }

int ui_scale_env(void)
{
	int s = scale_from(getenv("GDK_SCALE"));
	return s ? s : scale_from(getenv("QT_SCALE_FACTOR"));
}
