#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include "atlas.h"
#include "platform.h"
#include "ui.h"

#define XFUNCS(F)                     \
	F(xcb_connect)                    \
	F(xcb_disconnect)                 \
	F(xcb_connection_has_error)       \
	F(xcb_get_setup)                  \
	F(xcb_setup_roots_iterator)       \
	F(xcb_generate_id)                \
	F(xcb_create_window)              \
	F(xcb_map_window)                 \
	F(xcb_change_property)            \
	F(xcb_intern_atom)                \
	F(xcb_intern_atom_reply)          \
	F(xcb_flush)                      \
	F(xcb_wait_for_event)             \
	F(xcb_poll_for_event)             \
	F(xcb_put_image)                  \
	F(xcb_get_maximum_request_length) \
	F(xcb_send_event)                 \
	F(xcb_change_window_attributes)   \
	F(xcb_open_font)                  \
	F(xcb_create_glyph_cursor)        \
	F(xcb_set_selection_owner)        \
	F(xcb_ungrab_pointer)             \
	F(xcb_get_property)               \
	F(xcb_get_property_reply)         \
	F(xcb_get_property_value)         \
	F(xcb_get_property_value_length)  \
	F(xcb_create_gc)                  \
	F(xcb_screen_next)

static struct
{
#define F(n) __typeof__(n) *n;
	XFUNCS(F)
#undef F
} x;

static int load(void)
{
	void *h = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!h)
		return 0;
#define F(n)                              \
	if (!(*(void **)&x.n = dlsym(h, #n))) \
		return 0;
	XFUNCS(F)
#undef F
	return 1;
}

enum
{
	A_PROTOCOLS,
	A_DELETE,
	A_NAME,
	A_UTF8,
	A_TYPE,
	A_DIALOG,
	A_MOTIF,
	A_CLIPBOARD,
	A_TARGETS,
	A_MOVERESIZE,
	A_COUNT
};
static const char *const atom_names[A_COUNT] = {
	"WM_PROTOCOLS",
	"WM_DELETE_WINDOW",
	"_NET_WM_NAME",
	"UTF8_STRING",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DIALOG",
	"_MOTIF_WM_HINTS",
	"CLIPBOARD",
	"TARGETS",
	"_NET_WM_MOVERESIZE",
};

typedef struct x11_s
{
	diag_ctx_t *ctx;
	xcb_connection_t *c;
	xcb_window_t win, root;
	xcb_atom_t atoms[A_COUNT];
	xcb_gcontext_t gc;
	xcb_font_t cursor_font;
	xcb_cursor_t cursors[4];
	uint8_t depth;
	int S, pw, ph, last_cur, rootx, rooty;
	uint32_t *out;
	char *clip;
	size_t cliplen;
} x11_t;

static int loaded;

static const int cursor_glyph[4] = {68, 152, 138, 16};

static int dpi_scale(x11_t *st)
{
	xcb_get_property_cookie_t ck = x.xcb_get_property(st->c, 0, st->root, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 16384);
	xcb_get_property_reply_t *r = x.xcb_get_property_reply(st->c, ck, NULL);
	int s = 0;
	if (r)
	{
		int n = x.xcb_get_property_value_length(r);
		char *buf = malloc((size_t)n + 1);
		if (buf)
		{
			memcpy(buf, x.xcb_get_property_value(r), (size_t)n);
			buf[n] = 0;
			char *p = strstr(buf, "Xft.dpi:");
			if (p)
			{
				double dpi = strtod(p + 8, NULL);
				if (dpi > 0)
					s = (int)(dpi * 120 / 96 + 0.5);
			}
			free(buf);
		}
		free(r);
	}
	return s;
}

static void draw(x11_t *st)
{
	rect_t sd, dd;
	if (!st->out || !ui_paint(st->ctx, &sd))
		return;
	ui_present(st->ctx, st->out, st->pw, st->ph, st->S, &sd, &dd);
	if (dd.y0 >= dd.y1)
		return;
	size_t maxb = (size_t)x.xcb_get_maximum_request_length(st->c) * 4 - 64;
	int rows = (int)(maxb / ((size_t)st->pw * 4));
	if (rows < 1)
		rows = 1;
	for (int y = dd.y0; y < dd.y1; y += rows)
	{
		int n = dd.y1 - y < rows ? dd.y1 - y : rows;
		x.xcb_put_image(st->c, XCB_IMAGE_FORMAT_Z_PIXMAP, st->win, st->gc, (uint16_t)st->pw, (uint16_t)n, 0, (int16_t)y, 0, st->depth,
						(uint32_t)((size_t)st->pw * n * 4), (const uint8_t *)(st->out + (size_t)y * st->pw));
	}
}

static void set_cursor(x11_t *st, int cur)
{
	if (cur == st->last_cur)
		return;
	st->last_cur = cur;
	if (!st->cursors[cur])
	{
		st->cursors[cur] = x.xcb_generate_id(st->c);
		x.xcb_create_glyph_cursor(st->c, st->cursors[cur], st->cursor_font, st->cursor_font, (uint16_t)cursor_glyph[cur],
								  (uint16_t)(cursor_glyph[cur] + 1), 0, 0, 0, 0xffff, 0xffff, 0xffff);
	}
	x.xcb_change_window_attributes(st->c, st->win, XCB_CW_CURSOR, &st->cursors[cur]);
}

static void move_resize(x11_t *st, int dir)
{
	x.xcb_ungrab_pointer(st->c, XCB_CURRENT_TIME);
	xcb_client_message_event_t ev;
	memset(&ev, 0, sizeof ev);
	ev.response_type = XCB_CLIENT_MESSAGE;
	ev.format = 32;
	ev.window = st->win;
	ev.type = st->atoms[A_MOVERESIZE];
	ev.data.data32[0] = (uint32_t)st->rootx;
	ev.data.data32[1] = (uint32_t)st->rooty;
	ev.data.data32[2] = (uint32_t)dir;
	ev.data.data32[3] = 1;
	ev.data.data32[4] = 1;
	x.xcb_send_event(st->c, 0, st->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, (const char *)&ev);
}

static void set_clipboard(x11_t *st, const char *t, size_t n)
{
	free(st->clip);
	st->clip = malloc(n + 1);
	if (!st->clip)
		return;
	memcpy(st->clip, t, n);
	st->cliplen = n;
	x.xcb_set_selection_owner(st->c, st->win, st->atoms[A_CLIPBOARD], XCB_CURRENT_TIME);
}

static void selection_request(x11_t *st, xcb_selection_request_event_t *e)
{
	xcb_selection_notify_event_t n;
	memset(&n, 0, sizeof n);
	n.response_type = XCB_SELECTION_NOTIFY;
	n.time = e->time;
	n.requestor = e->requestor;
	n.selection = e->selection;
	n.target = e->target;
	n.property = e->property ? e->property : e->target;
	if (st->clip && e->selection == st->atoms[A_CLIPBOARD])
	{
		if (e->target == st->atoms[A_TARGETS])
		{
			xcb_atom_t t[3] = {st->atoms[A_TARGETS], st->atoms[A_UTF8], XCB_ATOM_STRING};
			x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, e->requestor, n.property, XCB_ATOM_ATOM, 32, 3, t);
		}
		else if (e->target == st->atoms[A_UTF8] || e->target == XCB_ATOM_STRING)
		{
			x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, e->requestor, n.property, e->target, 8, (uint32_t)st->cliplen, st->clip);
		}
		else
			n.property = XCB_ATOM_NONE;
	}
	else
		n.property = XCB_ATOM_NONE;
	x.xcb_send_event(st->c, 0, e->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&n);
}

static int lx(x11_t *st, int v) { return v * 120 / st->S; }

static void handle(x11_t *st, xcb_generic_event_t *ev)
{
	switch (ev->response_type & 0x7f)
	{
	case XCB_EXPOSE:
		ui_invalidate(st->ctx);
		break;
	case XCB_CONFIGURE_NOTIFY:
	{
		xcb_configure_notify_event_t *e = (void *)ev;
		if (e->width && e->height && (e->width != st->pw || e->height != st->ph))
		{
			st->pw = e->width;
			st->ph = e->height;
			free(st->out);
			st->out = calloc((size_t)st->pw * st->ph, 4);
			ui_set_height(st->ctx, lx(st, st->ph));
			ui_invalidate(st->ctx);
		}
		break;
	}
	case XCB_MOTION_NOTIFY:
	{
		xcb_motion_notify_event_t *e = (void *)ev;
		st->rootx = e->root_x;
		st->rooty = e->root_y;
		ui_pointer(st->ctx, lx(st, e->event_x), lx(st, e->event_y));
		break;
	}
	case XCB_BUTTON_PRESS:
	{
		xcb_button_press_event_t *e = (void *)ev;
		st->rootx = e->root_x;
		st->rooty = e->root_y;
		ui_pointer(st->ctx, lx(st, e->event_x), lx(st, e->event_y));
		if (e->detail == 1)
			ui_button(st->ctx, 1, e->time);
		else if (e->detail == 4)
			ui_wheel(st->ctx, -300);
		else if (e->detail == 5)
			ui_wheel(st->ctx, 300);
		break;
	}
	case XCB_BUTTON_RELEASE:
	{
		xcb_button_release_event_t *e = (void *)ev;
		ui_pointer(st->ctx, lx(st, e->event_x), lx(st, e->event_y));
		if (e->detail == 1)
			ui_button(st->ctx, 0, e->time);
		break;
	}
	case XCB_LEAVE_NOTIFY:
		ui_leave(st->ctx);
		break;
	case XCB_KEY_PRESS:
		ui_key(st->ctx, ((xcb_key_press_event_t *)ev)->detail - 8, 1);
		break;
	case XCB_KEY_RELEASE:
		ui_key(st->ctx, ((xcb_key_release_event_t *)ev)->detail - 8, 0);
		break;
	case XCB_FOCUS_OUT:
		ui_focus_lost(st->ctx);
		break;
	case XCB_CLIENT_MESSAGE:
	{
		xcb_client_message_event_t *e = (void *)ev;
		if (e->type == st->atoms[A_PROTOCOLS] && e->data.data32[0] == st->atoms[A_DELETE])
		{
			ui_key(st->ctx, 1, 1);
		}
		break;
	}
	case XCB_SELECTION_REQUEST:
		selection_request(st, (void *)ev);
		break;
	}
}

static void after(x11_t *st)
{
	int edge;
	int a = ui_take_action(st->ctx, &edge);
	if (a == ACT_MOVE)
		move_resize(st, 8);
	else if (a == ACT_RESIZE)
		move_resize(st, edge == EDGE_T ? 1 : 5);
	size_t n;
	const char *t = ui_take_copy(st->ctx, &n);
	if (t)
		set_clipboard(st, t, n);
	set_cursor(st, ui_cursor(st->ctx));
	draw(st);
	x.xcb_flush(st->c);
}

int plat_load_x11(void)
{
	return loaded = load();
}

int plat_run_x11(diag_ctx_t *ctx)
{
	if (!loaded)
		return 1;
	x11_t s = {.ctx = ctx, .S = 120, .last_cur = -1}, *st = &s;
	int scr;
	st->c = x.xcb_connect(NULL, &scr);
	if (!st->c || x.xcb_connection_has_error(st->c))
	{
		if (st->c)
			x.xcb_disconnect(st->c);
		return 1;
	}
	xcb_screen_iterator_t it = x.xcb_setup_roots_iterator(x.xcb_get_setup(st->c));
	while (scr-- > 0)
		x.xcb_screen_next(&it);
	xcb_screen_t *screen = it.data;
	st->root = screen->root;
	st->depth = screen->root_depth;

	xcb_intern_atom_cookie_t ck[A_COUNT];
	for (int i = 0; i < A_COUNT; i++)
		ck[i] = x.xcb_intern_atom(st->c, 0, (uint16_t)strlen(atom_names[i]), atom_names[i]);
	for (int i = 0; i < A_COUNT; i++)
	{
		xcb_intern_atom_reply_t *r = x.xcb_intern_atom_reply(st->c, ck[i], NULL);
		st->atoms[i] = r ? r->atom : XCB_ATOM_NONE;
		free(r);
	}

	st->S = ui_scale_override();
	if (!st->S)
		st->S = dpi_scale(st);
	if (!st->S)
		st->S = ui_scale_env();
	if (!st->S)
		st->S = 120;
	if (st->S < 120)
		st->S = 120;
	if (st->S > 1200)
		st->S = 1200;

	int w, h, minh, maxh;
	ui_size(st->ctx, &w, &h);
	ui_limits(&minh, &maxh);
	st->pw = (w * st->S + 119) / 120;
	st->ph = (h * st->S + 119) / 120;
	int px = (screen->width_in_pixels - st->pw) / 2, py = (screen->height_in_pixels - st->ph) / 2;
	if (px < 0)
		px = 0;
	if (py < 0)
		py = 0;
	st->out = calloc((size_t)st->pw * st->ph, 4);
	if (!st->out)
	{
		x.xcb_disconnect(st->c);
		return 1;
	}

	st->win = x.xcb_generate_id(st->c);
	uint32_t vals[2] = {0, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS |
							   XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW |
							   XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_FOCUS_CHANGE};
	vals[0] = g_bg_color.R << 16 | g_bg_color.G << 8 | g_bg_color.B;
	x.xcb_create_window(st->c, XCB_COPY_FROM_PARENT, st->win, st->root, (int16_t)px, (int16_t)py, (uint16_t)st->pw, (uint16_t)st->ph, 0,
						XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, vals);

	const char *title = ui_title(st->ctx);
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, (uint32_t)strlen(title), title);
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, st->atoms[A_NAME], st->atoms[A_UTF8], 8, (uint32_t)strlen(title), title);
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, 12, "sdiag\0sdiag\0");
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, st->atoms[A_PROTOCOLS], XCB_ATOM_ATOM, 32, 1, &st->atoms[A_DELETE]);
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, st->atoms[A_TYPE], XCB_ATOM_ATOM, 32, 1, &st->atoms[A_DIALOG]);

	uint32_t motif[5] = {2, 0, 0, 0, 0};
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, st->atoms[A_MOTIF], st->atoms[A_MOTIF], 32, 5, motif);
	uint32_t wmh[9] = {1, 1};
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS, 32, 9, wmh);

	uint32_t sh[18] = {0};
	sh[0] = 1 | 2 | 16 | 32;
	sh[1] = (uint32_t)px;
	sh[2] = (uint32_t)py;
	sh[3] = (uint32_t)st->pw;
	sh[4] = (uint32_t)st->ph;
	sh[5] = (uint32_t)st->pw;
	sh[6] = (uint32_t)((minh * st->S + 119) / 120);
	sh[7] = (uint32_t)st->pw;
	sh[8] = (uint32_t)((maxh * st->S + 119) / 120);
	x.xcb_change_property(st->c, XCB_PROP_MODE_REPLACE, st->win, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, sh);

	st->gc = x.xcb_generate_id(st->c);
	x.xcb_create_gc(st->c, st->gc, st->win, 0, NULL);
	st->cursor_font = x.xcb_generate_id(st->c);
	x.xcb_open_font(st->c, st->cursor_font, 6, "cursor");
	x.xcb_map_window(st->c, st->win);
	x.xcb_flush(st->c);

	xcb_generic_event_t *ev;
	while (!ui_done(st->ctx) && (ev = x.xcb_wait_for_event(st->c)))
	{
		do
		{
			handle(st, ev);
			free(ev);
		} while (!ui_done(st->ctx) && (ev = x.xcb_poll_for_event(st->c)));
		after(st);
	}

	free(st->out);
	free(st->clip);
	x.xcb_disconnect(st->c);
	return 0;
}
