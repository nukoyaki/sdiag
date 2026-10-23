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

static diag_ctx_t g_ctx;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int inr(rect_t r, int x, int y) { return x >= r.x0 && y >= r.y0 && x < r.x1 && y < r.y1; }
static int empty(rect_t r) { return r.x0 >= r.x1 || r.y0 >= r.y1; }
static int isect(rect_t a, rect_t b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static rect_t clipto(rect_t a, rect_t b) { return (rect_t){imax(a.x0, b.x0), imax(a.y0, b.y0), imin(a.x1, b.x1), imin(a.y1, b.y1)}; }

static void mark(rect_t r)
{
	if (empty(r))
		return;
	if (empty(g_ctx.dirty))
		g_ctx.dirty = r;
	else
		g_ctx.dirty = (rect_t){imin(g_ctx.dirty.x0, r.x0), imin(g_ctx.dirty.y0, r.y0), imax(g_ctx.dirty.x1, r.x1), imax(g_ctx.dirty.y1, r.y1)};
}

static rect_t title_r(void) { return (rect_t){0, 0, g_ctx.W, TITLE_H}; }
static rect_t body_r(void) { return (rect_t){0, TITLE_H, g_ctx.W, g_ctx.H - BOTTOM_H}; }
static rect_t bottom_r(void) { return (rect_t){0, g_ctx.H - BOTTOM_H, g_ctx.W, g_ctx.H}; }

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

static int btn_w(int i) { return imax(BTN_MIN_W, 2 * BTN_PAD_X + g_ctx.bcells[i] * FONT_W); }

static int btn_row(void)
{
	int w = 2 * BTN_MARGIN;
	for (int i = 0; i < g_ctx.nbtn; i++)
		w += btn_w(i) + (i ? BTN_GAP : 0);
	return w;
}

static void wrap(int maxcols)
{
	const uint32_t *c = g_ctx.cps;
	int i = 0, n = g_ctx.ncps;
	g_ctx.nlines = 0;
	g_ctx.nshown = n;
	do
	{
		if (g_ctx.nlines == MAX_LINES)
		{
			g_ctx.nshown = i;
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
		g_ctx.lines[g_ctx.nlines++] = ln;
	} while (i < n);
}

static int sel_lo(void) { return imin(g_ctx.sel_a, g_ctx.sel_b); }
static int sel_hi(void) { return imax(g_ctx.sel_a, g_ctx.sel_b); }

static int line_of(int idx)
{
	int i = g_ctx.nlines - 1;
	while (i > 0 && g_ctx.lines[i].start > idx)
		i--;
	return i;
}

static rect_t line_rect(int i)
{
	int y = TITLE_H + TEXT_Y + i * LINE_H - g_ctx.scroll;
	return clipto((rect_t){TEXT_X, y, TEXT_X + g_ctx.body_sprite.width, y + LINE_H}, body_r());
}

static void render_line(int i)
{
	const line_t *ln = &g_ctx.lines[i];
	int lo = sel_lo(), hi = sel_hi(), n = ln->end - ln->start;
	paint_target(&g_ctx.body_sprite);
	paint_text_sel(g_ctx.cps + ln->start, n, 0, i * LINE_H, lo - ln->start, hi - ln->start, g_ctx.body_sprite.width);
	if (ln->nl && ln->end >= lo && ln->end < hi && n < g_ctx.cols)
		paint_fill((rect_t){n * FONT_W, i * LINE_H, (n + 1) * FONT_W, (i + 1) * LINE_H}, g_bg_sel_color);
}

static void set_sel(int a, int b)
{
	int olo = sel_lo(), ohi = sel_hi();
	g_ctx.sel_a = a;
	g_ctx.sel_b = b;
	if (olo == sel_lo() && ohi == sel_hi())
		return;
	int l0 = line_of(imin(olo, sel_lo())), l1 = line_of(imax(ohi, sel_hi()));
	for (int i = l0; i <= l1; i++)
	{
		render_line(i);
		mark(line_rect(i));
	}
}

static void layout_buttons(void)
{
	int r = g_ctx.W - BTN_MARGIN, y = g_ctx.H - BOTTOM_H + BTN_Y;
	for (int i = 0; i < g_ctx.nbtn; i++)
	{
		int w = btn_w(i);
		g_ctx.btn_r[i] = (rect_t){r - w, y, r, y + BTN_H};
		r -= w + BTN_GAP;
	}
}

static void layout(void)
{
	g_ctx.vh = g_ctx.H - TITLE_H - BOTTOM_H;
	g_ctx.total = imax(TEXT_Y + g_ctx.nlines * LINE_H, ICON_Y + g_spr_info.height) + TEXT_PAD_BOTTOM;
	g_ctx.maxscroll = imax(g_ctx.total - g_ctx.vh, 0);
	g_ctx.sbar = g_ctx.total > g_ctx.vh;
	if (g_ctx.scroll > g_ctx.maxscroll)
		g_ctx.scroll = g_ctx.maxscroll;

	int trk = g_ctx.vh - 2 * SCROLL_TRACK_PAD;
	int fixed = g_spr_section_scroll_piece_start.height + g_spr_section_scroll_piece_end.height;
	int minth = fixed + g_spr_section_scroll_piece_middle.height;
	int th = g_ctx.total ? trk * g_ctx.vh / g_ctx.total : trk;
	th = fixed + (imin(th, trk) - fixed) / 4 * 4;
	th = imax(imin(th, fixed + (trk - fixed) / 4 * 4), minth);
	int ty = TITLE_H + SCROLL_TRACK_PAD + (g_ctx.maxscroll ? (trk - th) * g_ctx.scroll / g_ctx.maxscroll : 0);
	g_ctx.track_r = (rect_t){g_ctx.W - SCROLL_W, TITLE_H + SCROLL_TRACK_PAD, g_ctx.W, TITLE_H + SCROLL_TRACK_PAD + trk};
	g_ctx.thumb_r = (rect_t){g_ctx.W - SCROLL_W, ty, g_ctx.W, ty + th};
	g_ctx.close_r = (rect_t){g_ctx.W - CLOSE_MARGIN - CLOSE_BOX, CLOSE_MARGIN, g_ctx.W - CLOSE_MARGIN, CLOSE_MARGIN + CLOSE_BOX};
	layout_buttons();
}

static void set_scroll(int s)
{
	s = imax(imin(s, g_ctx.maxscroll), 0);
	if (s == g_ctx.scroll)
		return;
	g_ctx.scroll = s;
	layout();
	mark(body_r());
}

static int alloc_screen(void)
{
	free(g_ctx.screen.data);
	g_ctx.screen.width = (unsigned short)g_ctx.W;
	g_ctx.screen.height = (unsigned short)g_ctx.H;
	g_ctx.screen.data = malloc((size_t)g_ctx.W * g_ctx.H * sizeof(pixel_t));
	return g_ctx.screen.data != NULL;
}

static int make_title_sprite(void)
{
	int avail = g_ctx.W - TITLE_TEXT_X - CLOSE_MARGIN - CLOSE_BOX - TITLE_TEXT_GAP;
	int cols = imax(avail / FONT_W, 0), n;
	uint32_t *all = decode(g_ctx.title, &n);
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
	g_ctx.title_sprite.width = (unsigned short)w;
	g_ctx.title_sprite.height = FONT_H;
	g_ctx.title_sprite.data = malloc((size_t)w * FONT_H * sizeof(pixel_t));
	if (!g_ctx.title_sprite.data)
		return 0;
	paint_target(&g_ctx.title_sprite);
	paint_fill((rect_t){0, 0, w, FONT_H}, g_bg2_color);
	paint_text(shown, k, 0, 0, g_text_color, g_bg2_color);
	return 1;
}

int ui_init(const char *title, const char *message, const char *const *buttons, int n, int icon)
{
	memset(&g_ctx, 0, sizeof g_ctx);
	if (!atlas_init())
		return 0;
	if (n > MAX_BUTTONS)
		n = MAX_BUTTONS;
	g_ctx.title = title ? title : "";
	g_ctx.message = message;
	g_ctx.btn = buttons;
	g_ctx.nbtn = n;
	g_ctx.icon = icon < 0 || icon > 2 ? 0 : icon;
	g_ctx.hover_btn = -1;
	g_ctx.result = -1;
	g_ctx.mx = g_ctx.my = -1;

	for (int i = 0; i < n; i++)
	{
		g_ctx.bcp[i] = decode(buttons[i], &g_ctx.bcells[i]);
		if (!g_ctx.bcp[i])
		{
			ui_free();
			return 0;
		}
	}
	int tn;
	uint32_t *t = decode(g_ctx.title, &tn);
	if (!t)
	{
		ui_free();
		return 0;
	}
	free(t);
	g_ctx.tcells = tn;

	size_t ml = strlen(message);
	g_ctx.cps = malloc((ml * TAB_COLS + 1) * sizeof *g_ctx.cps);
	g_ctx.lines = malloc((ml * TAB_COLS + 2) * sizeof *g_ctx.lines);
	if (!g_ctx.cps || !g_ctx.lines)
	{
		ui_free();
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
				g_ctx.cps[g_ctx.ncps++] = ' ';
				col++;
			} while (col % TAB_COLS);
		}
		else if (c == '\n')
		{
			g_ctx.cps[g_ctx.ncps++] = c;
			col = 0;
		}
		else
		{
			g_ctx.cps[g_ctx.ncps++] = c;
			col++;
		}
		if (col > g_ctx.maxcells)
			g_ctx.maxcells = col;
	}

	int minw = imax(WIN_MIN_W, btn_row()), maxw = imax(WIN_MAX_W, minw);
	int want = imax(TEXT_X + TEXT_RIGHT_MARGIN + g_ctx.maxcells * FONT_W,
					TITLE_TEXT_X + g_ctx.tcells * FONT_W + TITLE_TEXT_GAP + CLOSE_BOX + CLOSE_MARGIN);
	g_ctx.W = imax(imin(want, maxw), minw);
	g_ctx.cols = imax((g_ctx.W - TEXT_X - TEXT_RIGHT_MARGIN) / FONT_W, 1);

	wrap(g_ctx.cols);
	int content = imax(TEXT_Y + g_ctx.nlines * LINE_H, ICON_Y + g_spr_info.height) + TEXT_PAD_BOTTOM;
	g_ctx.H = imax(imin(TITLE_H + content + BOTTOM_H, INIT_MAX_H), WIN_MIN_H);

	g_ctx.body_sprite.width = (unsigned short)(g_ctx.cols * FONT_W);
	g_ctx.body_sprite.height = (unsigned short)(g_ctx.nlines * LINE_H);
	g_ctx.body_sprite.data = malloc((size_t)g_ctx.body_sprite.width * g_ctx.body_sprite.height * sizeof(pixel_t));
	if (!g_ctx.body_sprite.data || !alloc_screen() || !make_title_sprite())
	{
		ui_free();
		return 0;
	}
	for (int i = 0; i < g_ctx.nlines; i++)
		render_line(i);

	layout();
	ui_invalidate();
	return 1;
}

void ui_free(void)
{
	for (int i = 0; i < MAX_BUTTONS; i++)
		free(g_ctx.bcp[i]);
	free(g_ctx.cps);
	free(g_ctx.lines);
	free(g_ctx.screen.data);
	free(g_ctx.title_sprite.data);
	free(g_ctx.body_sprite.data);
	free(g_ctx.copybuf);
	atlas_free();
	memset(&g_ctx, 0, sizeof g_ctx);
}

const char *ui_title(void) { return g_ctx.title; }

void ui_size(int *w, int *h)
{
	*w = g_ctx.W;
	*h = g_ctx.H;
}

void ui_limits(int *minh, int *maxh)
{
	*minh = WIN_MIN_H;
	*maxh = imax(WIN_MAX_H, WIN_MIN_H);
}

void ui_set_height(int h)
{
	h = imax(imin(h, WIN_MAX_H), WIN_MIN_H);
	if (h == g_ctx.H)
		return;
	g_ctx.H = h;
	if (!alloc_screen())
	{
		g_ctx.done = 1;
		return;
	}
	layout();
	ui_invalidate();
}

void ui_invalidate(void)
{
	g_ctx.dirty = (rect_t){0, 0, g_ctx.W, g_ctx.H};
}

static const sprite_t *icon_sprite(void)
{
	return g_ctx.icon == 1 ? &g_spr_warning : g_ctx.icon == 2 ? &g_spr_error
													  : &g_spr_info;
}

static void paint_title(rect_t d)
{
	rect_t t = title_r();
	paint_clip(clipto(d, t));
	paint_bar_top(t);
	paint_blit(&g_ctx.title_sprite, TITLE_TEXT_X, TITLE_TEXT_Y);
	int st = g_ctx.hover_close ? (g_ctx.down == T_CLOSE ? EMBOSS_DOWN : EMBOSS_HOVER) : EMBOSS_NORMAL;
	paint_emboss(g_ctx.close_r, st);
	paint_blit_keyed(&g_spr_close, g_ctx.close_r.x0 + CLOSE_SPRITE_INSET, g_ctx.close_r.y0 + CLOSE_SPRITE_INSET);
}

static void paint_body(rect_t d)
{
	rect_t b = body_r();
	paint_clip(clipto(d, b));
	paint_fill(b, g_bg_color);
	paint_blit(icon_sprite(), ICON_X, TITLE_H + ICON_Y - g_ctx.scroll);
	paint_blit(&g_ctx.body_sprite, TEXT_X, TITLE_H + TEXT_Y - g_ctx.scroll);
	if (g_ctx.scroll > 0)
		paint_fade((rect_t){0, b.y0, g_ctx.W, b.y0 + FADE_H}, g_bg_color, 1);
	if (g_ctx.scroll < g_ctx.maxscroll)
		paint_fade((rect_t){0, b.y1 - FADE_H, g_ctx.W, b.y1}, g_bg_color, 0);
	if (g_ctx.sbar)
		paint_scrollbar(g_ctx.thumb_r.x0, g_ctx.thumb_r.y0, g_ctx.thumb_r.y1 - g_ctx.thumb_r.y0);
}

static void paint_bottom(rect_t d)
{
	rect_t b = bottom_r();
	paint_clip(clipto(d, b));
	paint_bar_bottom(b);
	for (int i = 0; i < g_ctx.nbtn; i++)
	{
		rect_t r = g_ctx.btn_r[i];
		int pressed = g_ctx.down == T_BTN && g_ctx.down_i == i && g_ctx.hover_btn == i;
		int st = pressed ? EMBOSS_DOWN : g_ctx.focus == i ? EMBOSS_HOVER
													  : EMBOSS_NORMAL;
		paint_emboss(r, st);
		paint_text(g_ctx.bcp[i], g_ctx.bcells[i], r.x0 + (r.x1 - r.x0 - g_ctx.bcells[i] * FONT_W) / 2, r.y0 + BTN_TEXT_Y,
				   g_text_color, paint_emboss_fill(st));
	}
}

int ui_paint(rect_t *out)
{
	if (empty(g_ctx.dirty))
		return 0;
	rect_t d = clipto(g_ctx.dirty, (rect_t){0, 0, g_ctx.W, g_ctx.H});
	g_ctx.dirty = (rect_t){0, 0, 0, 0};
	if (empty(d))
		return 0;
	paint_target(&g_ctx.screen);
	if (isect(d, title_r()))
		paint_title(d);
	if (isect(d, body_r()))
		paint_body(d);
	if (isect(d, bottom_r()))
		paint_bottom(d);
	*out = d;
	return 1;
}

void ui_present(uint32_t *out, int ow, int oh, int S, const rect_t *sd, rect_t *dd)
{
	paint_present(&g_ctx.screen, out, ow, oh, S, sd, dd);
}

static int hit_btn(int x, int y)
{
	for (int i = 0; i < g_ctx.nbtn; i++)
		if (inr(g_ctx.btn_r[i], x, y))
			return i;
	return -1;
}

static int hit_edge(int x, int y)
{
	(void)x;
	if (y >= 0 && y < RESIZE_ZONE)
		return EDGE_T;
	if (y >= g_ctx.H - RESIZE_ZONE && y < g_ctx.H)
		return EDGE_B;
	return 0;
}

static int in_text_area(int x, int y)
{
	return y >= TITLE_H && y < g_ctx.H - BOTTOM_H && x >= 0 && x < g_ctx.W - SCROLL_W;
}

static int line_at(int y)
{
	int v = y - (TITLE_H + TEXT_Y) + g_ctx.scroll, li = v < 0 ? 0 : v / LINE_H;
	return li >= g_ctx.nlines ? g_ctx.nlines - 1 : li;
}

static int idx_at(int x, int y, int caret)
{
	const line_t *ln = &g_ctx.lines[line_at(y)];
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

static void select_word(int x, int y)
{
	const line_t *ln = &g_ctx.lines[line_at(y)];
	if (ln->end == ln->start)
		return;
	int c = idx_at(x, y, 0), k = cls(g_ctx.cps[c]), lo = c, hi = c + 1;
	while (lo > ln->start && cls(g_ctx.cps[lo - 1]) == k)
		lo--;
	while (hi < ln->end && cls(g_ctx.cps[hi]) == k)
		hi++;
	set_sel(lo, hi);
}

static void finish(int r)
{
	g_ctx.result = r;
	g_ctx.done = 1;
}

static void set_focus(int i)
{
	if (i == g_ctx.focus)
		return;
	mark(g_ctx.btn_r[g_ctx.focus]);
	g_ctx.focus = i;
	mark(g_ctx.btn_r[i]);
}

void ui_pointer(int x, int y)
{
	g_ctx.mx = x;
	g_ctx.my = y;
	int hb = hit_btn(x, y), hc = inr(g_ctx.close_r, x, y);
	if (hc != g_ctx.hover_close)
	{
		g_ctx.hover_close = hc;
		mark(g_ctx.close_r);
	}
	if (hb != g_ctx.hover_btn)
	{
		if (g_ctx.hover_btn >= 0)
			mark(g_ctx.btn_r[g_ctx.hover_btn]);
		g_ctx.hover_btn = hb;
		if (hb >= 0)
			mark(g_ctx.btn_r[hb]);
	}
	if (hb >= 0)
		set_focus(hb);

	if (g_ctx.down == T_TEXT)
	{
		if (y < TITLE_H)
			set_scroll(g_ctx.scroll - LINE_H);
		else if (y >= g_ctx.H - BOTTOM_H)
			set_scroll(g_ctx.scroll + LINE_H);
		set_sel(g_ctx.sel_a, idx_at(x, y, 1));
	}
	else if (g_ctx.down == T_THUMB)
	{
		int range = g_ctx.track_r.y1 - g_ctx.track_r.y0 - (g_ctx.thumb_r.y1 - g_ctx.thumb_r.y0);
		if (range > 0)
			set_scroll((y - g_ctx.dragoff - g_ctx.track_r.y0) * g_ctx.maxscroll / range);
	}
}

void ui_leave(void)
{
	g_ctx.mx = g_ctx.my = -1;
	if (g_ctx.hover_close)
	{
		g_ctx.hover_close = 0;
		mark(g_ctx.close_r);
	}
	if (g_ctx.hover_btn >= 0)
	{
		mark(g_ctx.btn_r[g_ctx.hover_btn]);
		g_ctx.hover_btn = -1;
	}
}

void ui_button(int pressed, unsigned time)
{
	int x = g_ctx.mx, y = g_ctx.my;
	if (!pressed)
	{
		if (g_ctx.down == T_CLOSE && inr(g_ctx.close_r, x, y))
			finish(-1);
		else if (g_ctx.down == T_BTN && hit_btn(x, y) == g_ctx.down_i)
			finish(g_ctx.down_i);
		if (g_ctx.down == T_CLOSE)
			mark(g_ctx.close_r);
		if (g_ctx.down == T_BTN)
			mark(g_ctx.btn_r[g_ctx.down_i]);
		g_ctx.down = T_NONE;
		return;
	}
	int b, e;
	if (inr(g_ctx.close_r, x, y))
	{
		g_ctx.down = T_CLOSE;
		mark(g_ctx.close_r);
	}
	else if ((e = hit_edge(x, y)))
	{
		g_ctx.action = ACT_RESIZE;
		g_ctx.edge = e;
	}
	else if ((b = hit_btn(x, y)) >= 0)
	{
		g_ctx.down = T_BTN;
		g_ctx.down_i = b;
		set_focus(b);
		mark(g_ctx.btn_r[b]);
	}
	else if (y < TITLE_H)
		g_ctx.action = ACT_MOVE;
	else if (g_ctx.sbar && inr(g_ctx.track_r, x, y))
	{
		if (inr(g_ctx.thumb_r, x, y))
		{
			g_ctx.down = T_THUMB;
			g_ctx.dragoff = y - g_ctx.thumb_r.y0;
		}
		else
			set_scroll(g_ctx.scroll + (y < g_ctx.thumb_r.y0 ? -1 : 1) * (g_ctx.vh * 9 / 10));
	}
	else if (in_text_area(x, y))
	{
		int dbl = g_ctx.last_valid && time - g_ctx.last_t < DOUBLE_CLICK_MS && abs(x - g_ctx.last_x) <= 4 && abs(y - g_ctx.last_y) <= 4;
		g_ctx.last_valid = !dbl;
		g_ctx.last_t = time;
		g_ctx.last_x = x;
		g_ctx.last_y = y;
		if (dbl)
			select_word(x, y);
		else
		{
			int i = idx_at(x, y, 1);
			g_ctx.down = T_TEXT;
			set_sel(i, i);
		}
	}
}

void ui_wheel(int lines_x100)
{
	set_scroll(g_ctx.scroll + lines_x100 * LINE_H / 100);
}

static void do_copy(void)
{
	free(g_ctx.copybuf);
	g_ctx.copybuf = NULL;
	g_ctx.copylen = 0;
	int lo = sel_lo(), hi = sel_hi();
	char *o;
	if (lo != hi)
	{
		o = g_ctx.copybuf = malloc((size_t)(hi - lo) * 4 + 1);
		if (!o)
			return;
		for (int i = lo; i < hi; i++)
			o = u8put(o, g_ctx.cps[i]);
	}
	else
	{
		size_t cap = strlen(g_ctx.title) + strlen(g_ctx.message) + 128;
		for (int i = 0; i < g_ctx.nbtn; i++)
			cap += strlen(g_ctx.btn[i]) + 8;
		o = g_ctx.copybuf = malloc(cap);
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
		APP(g_ctx.title);
		APP(" ---\n\n");
		APP(g_ctx.message);
		APP("\n------------------------------------------------\n\n{ ");
		for (int i = g_ctx.nbtn - 1; i >= 0; i--)
		{
			APP(g_ctx.btn[i]);
			if (i)
				APP(" } { ");
		}
		APP(" }");
#undef APP
	}
	g_ctx.copylen = (size_t)(o - g_ctx.copybuf);
	g_ctx.copy = 1;
}

void ui_key(int k, int down)
{
	switch (k)
	{
	case 29:
	case 97:
		g_ctx.ctrl = down;
		return;
	case 42:
	case 54:
		g_ctx.shift = down;
		return;
	}
	if (!down)
		return;
	switch (k)
	{
	case 1:
		finish(-1);
		break;
	case 28:
	case 96:
	case 57:
		finish(g_ctx.focus);
		break;
	case 105:
		if (g_ctx.focus < g_ctx.nbtn - 1)
			set_focus(g_ctx.focus + 1);
		break;
	case 106:
		if (g_ctx.focus > 0)
			set_focus(g_ctx.focus - 1);
		break;
	case 15:
		set_focus(g_ctx.shift ? (g_ctx.focus + g_ctx.nbtn - 1) % g_ctx.nbtn : (g_ctx.focus + 1) % g_ctx.nbtn);
		break;
	case 30:
		if (g_ctx.ctrl)
			set_sel(0, g_ctx.nshown);
		break;
	case 46:
		if (g_ctx.ctrl)
			do_copy();
		break;
	case 103:
		set_scroll(g_ctx.scroll - LINE_H);
		break;
	case 108:
		set_scroll(g_ctx.scroll + LINE_H);
		break;
	case 104:
		set_scroll(g_ctx.scroll - g_ctx.vh * 9 / 10);
		break;
	case 109:
		set_scroll(g_ctx.scroll + g_ctx.vh * 9 / 10);
		break;
	case 102:
		set_scroll(0);
		break;
	case 107:
		set_scroll(g_ctx.maxscroll);
		break;
	}
}

void ui_focus_lost(void)
{
	g_ctx.ctrl = g_ctx.shift = 0;
}

int ui_done(void) { return g_ctx.done; }
int ui_result(void) { return g_ctx.result; }

int ui_take_action(int *edge)
{
	int a = g_ctx.action;
	*edge = g_ctx.edge;
	g_ctx.action = ACT_NONE;
	return a;
}

const char *ui_take_copy(size_t *len)
{
	if (!g_ctx.copy)
		return NULL;
	g_ctx.copy = 0;
	*len = g_ctx.copylen;
	return g_ctx.copybuf;
}

int ui_cursor(void)
{
	if (g_ctx.down == T_TEXT)
		return CUR_TEXT;
	if (g_ctx.down != T_NONE || inr(g_ctx.close_r, g_ctx.mx, g_ctx.my) || hit_btn(g_ctx.mx, g_ctx.my) >= 0)
		return CUR_ARROW;
	int e = hit_edge(g_ctx.mx, g_ctx.my);
	if (e)
		return e == EDGE_T ? CUR_RESIZE_T : CUR_RESIZE_B;
	return in_text_area(g_ctx.mx, g_ctx.my) ? CUR_TEXT : CUR_ARROW;
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
