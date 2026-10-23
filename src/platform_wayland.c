#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define wl_proxy_marshal_flags sd_wl_proxy_marshal_flags
#define wl_proxy_add_listener sd_wl_proxy_add_listener
#define wl_proxy_destroy sd_wl_proxy_destroy
#define wl_proxy_get_version sd_wl_proxy_get_version
#define wl_proxy_set_user_data sd_wl_proxy_set_user_data
#define wl_proxy_get_user_data sd_wl_proxy_get_user_data

#include <wayland-client.h>
#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "platform.h"
#include "ui.h"

static struct
{
	struct wl_display *(*display_connect)(const char *);
	void (*display_disconnect)(struct wl_display *);
	int (*display_dispatch)(struct wl_display *);
	int (*display_roundtrip)(struct wl_display *);
	int (*display_flush)(struct wl_display *);
	struct wl_proxy *(*marshal_array_flags)(struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, uint32_t, union wl_argument *);
	int (*add_listener)(struct wl_proxy *, void (**)(void), void *);
	void (*destroy)(struct wl_proxy *);
	uint32_t (*get_version)(struct wl_proxy *);
	void (*set_user_data)(struct wl_proxy *, void *);
	void *(*get_user_data)(struct wl_proxy *);
} wl;

static int load(void)
{
	void *h = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
	if (!h)
		return 0;
#define F(m, n)                           \
	if (!(*(void **)&wl.m = dlsym(h, n))) \
		return 0;
	F(display_connect, "wl_display_connect")
	F(display_disconnect, "wl_display_disconnect")
	F(display_dispatch, "wl_display_dispatch")
	F(display_roundtrip, "wl_display_roundtrip")
	F(display_flush, "wl_display_flush")
	F(marshal_array_flags, "wl_proxy_marshal_array_flags")
	F(add_listener, "wl_proxy_add_listener")
	F(destroy, "wl_proxy_destroy")
	F(get_version, "wl_proxy_get_version")
	F(set_user_data, "wl_proxy_set_user_data")
	F(get_user_data, "wl_proxy_get_user_data")
#undef F
	return 1;
}

/* the generated headers call these, so they forward to the dlopen'd library */
struct wl_proxy *wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode, const struct wl_interface *iface,
										uint32_t version, uint32_t flags, ...)
{
	union wl_argument args[20];
	const struct wl_interface *pi = *(const struct wl_interface **)proxy;
	const char *sig = pi->methods[opcode].signature;
	va_list ap;
	va_start(ap, flags);
	for (int i = 0; *sig; sig++)
	{
		switch (*sig)
		{
		case 'i':
			args[i++].i = va_arg(ap, int32_t);
			break;
		case 'u':
			args[i++].u = va_arg(ap, uint32_t);
			break;
		case 'f':
			args[i++].f = va_arg(ap, wl_fixed_t);
			break;
		case 's':
			args[i++].s = va_arg(ap, const char *);
			break;
		case 'o':
		case 'n':
			args[i++].o = va_arg(ap, struct wl_object *);
			break;
		case 'a':
			args[i++].a = va_arg(ap, struct wl_array *);
			break;
		case 'h':
			args[i++].h = va_arg(ap, int32_t);
			break;
		}
	}
	va_end(ap);
	return wl.marshal_array_flags(proxy, opcode, iface, version, flags, args);
}

int wl_proxy_add_listener(struct wl_proxy *p, void (**impl)(void), void *data) { return wl.add_listener(p, impl, data); }
void wl_proxy_destroy(struct wl_proxy *p) { wl.destroy(p); }
uint32_t wl_proxy_get_version(struct wl_proxy *p) { return wl.get_version(p); }
void wl_proxy_set_user_data(struct wl_proxy *p, void *d) { wl.set_user_data(p, d); }
void *wl_proxy_get_user_data(struct wl_proxy *p) { return wl.get_user_data(p); }

/* cursor-shape-v1 refers to the tablet tool interface, which is never used here */
const struct wl_interface zwp_tablet_tool_v2_interface = {"zwp_tablet_tool_v2", 1, 0, NULL, 0, NULL};

/* buffers the compositor hasn't released yet, a frame is skipped until one frees up if they're all in use */
#define MAX_BUFS 8

typedef struct way_s
{
	diag_ctx_t *ctx;
	struct wl_display *dpy;
	struct wl_registry *reg;
	struct wl_compositor *comp;
	struct wl_shm *shm;
	struct xdg_wm_base *wm;
	struct wl_seat *seat;
	struct wl_data_device_manager *ddm;
	struct wl_data_device *ddev;
	struct wl_data_source *dsrc;
	struct wl_data_offer *offer;
	struct wp_fractional_scale_manager_v1 *fsm;
	struct wp_viewporter *vpr;
	struct wp_cursor_shape_manager_v1 *csm;
	struct wl_surface *surf;
	struct xdg_surface *xs;
	struct xdg_toplevel *top;
	struct wp_viewport *vp;
	struct wp_fractional_scale_v1 *fs;
	struct wl_pointer *ptr;
	struct wl_keyboard *kbd;
	struct wp_cursor_shape_device_v1 *csd;
	struct wl_buffer *bufs[MAX_BUFS];

	int S, scale_locked, configured, force, cfg_h, last_cur;
	uint32_t enter_serial, btn_serial, key_serial;
	char *clip;
	size_t cliplen;
} way_t;

static int loaded;

static void set_limits(way_t *st)
{
	int w, h, minh, maxh;
	ui_size(st->ctx, &w, &h);
	ui_limits(&minh, &maxh);
	xdg_toplevel_set_min_size(st->top, w, minh);
	xdg_toplevel_set_max_size(st->top, w, maxh);
}

static void buf_release(void *d, struct wl_buffer *b)
{
	way_t *st = d;
	for (int i = 0; i < MAX_BUFS; i++)
		if (st->bufs[i] == b)
			st->bufs[i] = NULL;
	wl_buffer_destroy(b);
}
static const struct wl_buffer_listener buf_l = {buf_release};

static void draw(way_t *st)
{
	int slot = 0;
	while (slot < MAX_BUFS && st->bufs[slot])
		slot++;
	if (slot == MAX_BUFS)
		return;

	rect_t sd, dd;
	int painted = ui_paint(st->ctx, &sd);
	if (!painted && !st->force)
		return;
	st->force = 0;

	int w, h;
	ui_size(st->ctx, &w, &h);
	int ow = (w * st->S + 60) / 120, oh = (h * st->S + 60) / 120;
	size_t sz = (size_t)ow * oh * 4;
	int fd = memfd_create("sdiag", MFD_CLOEXEC);
	if (fd < 0)
		return;
	if (ftruncate(fd, (off_t)sz))
	{
		close(fd);
		return;
	}
	void *m = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED)
	{
		close(fd);
		return;
	}
	ui_present(st->ctx, m, ow, oh, st->S, NULL, &dd);
	munmap(m, sz);

	struct wl_shm_pool *pool = wl_shm_create_pool(st->shm, fd, (int32_t)sz);
	struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, ow, oh, ow * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b, &buf_l, st);
	st->bufs[slot] = b;

	if (st->vp)
		wp_viewport_set_destination(st->vp, w, h);
	wl_surface_attach(st->surf, b, 0, 0);
	if (wl_surface_get_version(st->surf) >= 4)
		wl_surface_damage_buffer(st->surf, 0, 0, ow, oh);
	else
		wl_surface_damage(st->surf, 0, 0, w, h);
	wl_surface_commit(st->surf);
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t n)
{
	(void)d;
	(void)r;
	(void)n;
}

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t s)
{
	(void)d;
	xdg_wm_base_pong(b, s);
}
static const struct xdg_wm_base_listener wm_l = {wm_ping};

static void seat_name(void *d, struct wl_seat *s, const char *n)
{
	(void)d;
	(void)s;
	(void)n;
}

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
	way_t *st = d;
	(void)p;
	(void)s;
	st->enter_serial = serial;
	st->last_cur = -1;
	ui_pointer(st->ctx, wl_fixed_to_int(x), wl_fixed_to_int(y));
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	way_t *st = d;
	(void)p;
	(void)serial;
	(void)s;
	ui_leave(st->ctx);
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	way_t *st = d;
	(void)p;
	(void)t;
	ui_pointer(st->ctx, wl_fixed_to_int(x), wl_fixed_to_int(y));
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t b, uint32_t state)
{
	way_t *st = d;
	(void)p;
	if (b != 0x110)
		return;
	st->btn_serial = serial;
	ui_button(st->ctx, state == WL_POINTER_BUTTON_STATE_PRESSED, t);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	way_t *st = d;
	(void)p;
	(void)t;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		ui_wheel(st->ctx, (int)(wl_fixed_to_double(v) * 30));
}
static const struct wl_pointer_listener ptr_l = {.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button, .axis = p_axis};

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t sz)
{
	(void)d;
	(void)k;
	(void)fmt;
	(void)sz;
	close(fd);
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys)
{
	(void)d;
	(void)k;
	(void)s;
	(void)sf;
	(void)keys;
}
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf)
{
	way_t *st = d;
	(void)k;
	(void)s;
	(void)sf;
	ui_focus_lost(st->ctx);
}
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
	way_t *st = d;
	(void)k;
	(void)t;
	st->key_serial = serial;
	ui_key(st->ctx, (int)key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g)
{
	(void)d;
	(void)k;
	(void)s;
	(void)a;
	(void)b;
	(void)c;
	(void)g;
}
static const struct wl_keyboard_listener kbd_l = {.keymap = k_keymap, .enter = k_enter, .leave = k_leave, .key = k_key, .modifiers = k_mods};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	way_t *st = d;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !st->ptr)
	{
		st->ptr = wl_seat_get_pointer(s);
		wl_pointer_add_listener(st->ptr, &ptr_l, st);
		if (st->csm)
			st->csd = wp_cursor_shape_manager_v1_get_pointer(st->csm, st->ptr);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !st->kbd)
	{
		st->kbd = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(st->kbd, &kbd_l, st);
	}
}
static const struct wl_seat_listener seat_l = {.capabilities = seat_caps, .name = seat_name};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	way_t *st = d;
	if (!strcmp(iface, wl_compositor_interface.name))
		st->comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		st->shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, xdg_wm_base_interface.name))
	{
		st->wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(st->wm, &wm_l, st);
	}
	else if (!strcmp(iface, wl_seat_interface.name) && !st->seat)
	{
		st->seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
		wl_seat_add_listener(st->seat, &seat_l, st);
	}
	else if (!strcmp(iface, wl_data_device_manager_interface.name))
		st->ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 1);
	else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name))
		st->fsm = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
	else if (!strcmp(iface, wp_viewporter_interface.name))
		st->vpr = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
	else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
		st->csm = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
}
static const struct wl_registry_listener reg_l = {reg_global, reg_remove};

static void xs_configure(void *d, struct xdg_surface *s, uint32_t serial)
{
	way_t *st = d;
	xdg_surface_ack_configure(s, serial);
	st->configured = 1;
	int w, h;
	ui_size(st->ctx, &w, &h);
	if (st->cfg_h > 0 && st->cfg_h != h)
	{
		ui_set_height(st->ctx, st->cfg_h);
		st->force = 1;
	}
}
static const struct xdg_surface_listener xs_l = {xs_configure};

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
	way_t *st = d;
	(void)t;
	(void)w;
	(void)states;
	st->cfg_h = h;
}
static void top_close(void *d, struct xdg_toplevel *t)
{
	way_t *st = d;
	(void)t;
	ui_key(st->ctx, 1, 1);
}
static const struct xdg_toplevel_listener top_l = {.configure = top_configure, .close = top_close};

static void fs_scale(void *d, struct wp_fractional_scale_v1 *f, uint32_t s)
{
	way_t *st = d;
	(void)f;
	if (st->scale_locked)
		return;
	st->S = s < 120 ? 120 : s > 1200 ? 1200
									 : (int)s;
	st->force = 1;
}
static const struct wp_fractional_scale_v1_listener fs_l = {fs_scale};

static void ds_target(void *d, struct wl_data_source *s, const char *m)
{
	(void)d;
	(void)s;
	(void)m;
}
static void ds_send(void *d, struct wl_data_source *s, const char *mime, int32_t fd)
{
	way_t *st = d;
	(void)s;
	(void)mime;
	for (size_t o = 0; o < st->cliplen;)
	{
		ssize_t n = write(fd, st->clip + o, st->cliplen - o);
		if (n <= 0)
			break;
		o += (size_t)n;
	}
	close(fd);
}
static void ds_cancelled(void *d, struct wl_data_source *s)
{
	way_t *st = d;
	wl_data_source_destroy(s);
	if (s == st->dsrc)
		st->dsrc = NULL;
}
static const struct wl_data_source_listener ds_l = {.target = ds_target, .send = ds_send, .cancelled = ds_cancelled};

/* offers from other clients are never read, but destroying one inside the event that created it leaks
   the proxy, so it is kept until the selection event, the next offer, or teardown */
static void dd_offer(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
	way_t *st = d;
	(void)dd;
	if (st->offer)
		wl_data_offer_destroy(st->offer);
	st->offer = o;
}
static void dd_enter(void *d, struct wl_data_device *dd, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{
	(void)d;
	(void)dd;
	(void)serial;
	(void)s;
	(void)x;
	(void)y;
	(void)o;
}
static void dd_leave(void *d, struct wl_data_device *dd)
{
	(void)d;
	(void)dd;
}
static void dd_motion(void *d, struct wl_data_device *dd, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	(void)dd;
	(void)t;
	(void)x;
	(void)y;
}
static void dd_drop(void *d, struct wl_data_device *dd)
{
	(void)d;
	(void)dd;
}
static void dd_selection(void *d, struct wl_data_device *dd, struct wl_data_offer *o)
{
	way_t *st = d;
	(void)dd;
	(void)o;
	if (st->offer)
		wl_data_offer_destroy(st->offer);
	st->offer = NULL;
}
static const struct wl_data_device_listener dd_l = {.data_offer = dd_offer, .enter = dd_enter, .leave = dd_leave, .motion = dd_motion, .drop = dd_drop, .selection = dd_selection};

static void set_clipboard(way_t *st, const char *t, size_t n)
{
	if (!st->ddm || !st->ddev)
		return;
	free(st->clip);
	st->clip = malloc(n + 1);
	if (!st->clip)
		return;
	memcpy(st->clip, t, n);
	st->cliplen = n;
	struct wl_data_source *s = wl_data_device_manager_create_data_source(st->ddm);
	wl_data_source_add_listener(s, &ds_l, st);
	wl_data_source_offer(s, "text/plain;charset=utf-8");
	wl_data_source_offer(s, "text/plain");
	wl_data_source_offer(s, "UTF8_STRING");
	wl_data_device_set_selection(st->ddev, s, st->key_serial);
	st->dsrc = s;
}

static uint32_t shape_for(int cur)
{
	switch (cur)
	{
	case CUR_TEXT:
		return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;
	case CUR_RESIZE_T:
		return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE;
	case CUR_RESIZE_B:
		return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
	}
	return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
}

static void post(way_t *st)
{
	int edge, a = ui_take_action(st->ctx, &edge);
	if (a == ACT_MOVE && st->seat)
		xdg_toplevel_move(st->top, st->seat, st->btn_serial);
	else if (a == ACT_RESIZE && st->seat)
		xdg_toplevel_resize(st->top, st->seat, st->btn_serial, edge == EDGE_T ? XDG_TOPLEVEL_RESIZE_EDGE_TOP : XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
	size_t n;
	const char *t = ui_take_copy(st->ctx, &n);
	if (t)
		set_clipboard(st, t, n);
	int cur = ui_cursor(st->ctx);
	if (st->csd && cur != st->last_cur)
	{
		wp_cursor_shape_device_v1_set_shape(st->csd, st->enter_serial, shape_for(cur));
		st->last_cur = cur;
	}
	draw(st);
}

static void teardown(way_t *st)
{
#define D(m, f) \
	if (st->m)  \
		f(st->m);
	D(dsrc, wl_data_source_destroy)
	D(offer, wl_data_offer_destroy)
	D(ddev, wl_data_device_destroy)
	D(csd, wp_cursor_shape_device_v1_destroy)
	D(ptr, wl_pointer_destroy)
	D(kbd, wl_keyboard_destroy)
	D(fs, wp_fractional_scale_v1_destroy)
	D(vp, wp_viewport_destroy)
	D(top, xdg_toplevel_destroy)
	D(xs, xdg_surface_destroy)
	D(surf, wl_surface_destroy)
	/* lets the compositor release the last buffers so buf_release destroys them */
	wl.display_roundtrip(st->dpy);
	for (int i = 0; i < MAX_BUFS; i++)
		D(bufs[i], wl_buffer_destroy)
	D(csm, wp_cursor_shape_manager_v1_destroy)
	D(vpr, wp_viewporter_destroy)
	D(fsm, wp_fractional_scale_manager_v1_destroy)
	D(ddm, wl_data_device_manager_destroy)
	D(seat, wl_seat_destroy)
	D(wm, xdg_wm_base_destroy)
	D(shm, wl_shm_destroy)
	D(comp, wl_compositor_destroy)
	D(reg, wl_registry_destroy)
#undef D
	wl.display_flush(st->dpy);
	wl.display_disconnect(st->dpy);
	free(st->clip);
}

int plat_load_wayland(void)
{
	return loaded = load();
}

int plat_run_wayland(diag_ctx_t *ctx)
{
	if (!loaded)
		return 1;
	way_t s = {.ctx = ctx, .S = 120, .last_cur = -1}, *st = &s;
	st->dpy = wl.display_connect(NULL);
	if (!st->dpy)
		return 1;

	st->reg = wl_display_get_registry(st->dpy);
	wl_registry_add_listener(st->reg, &reg_l, st);
	wl.display_roundtrip(st->dpy);
	if (!st->comp || !st->shm || !st->wm)
	{
		teardown(st);
		return 1;
	}
	if (st->seat && st->ddm)
	{
		st->ddev = wl_data_device_manager_get_data_device(st->ddm, st->seat);
		wl_data_device_add_listener(st->ddev, &dd_l, st);
	}

	st->surf = wl_compositor_create_surface(st->comp);
	st->xs = xdg_wm_base_get_xdg_surface(st->wm, st->surf);
	xdg_surface_add_listener(st->xs, &xs_l, st);
	st->top = xdg_surface_get_toplevel(st->xs);
	xdg_toplevel_add_listener(st->top, &top_l, st);
	xdg_toplevel_set_title(st->top, ui_title(st->ctx));
	xdg_toplevel_set_app_id(st->top, "sdiag");
	if (st->fsm && st->vpr)
	{
		st->vp = wp_viewporter_get_viewport(st->vpr, st->surf);
		st->fs = wp_fractional_scale_manager_v1_get_fractional_scale(st->fsm, st->surf);
		wp_fractional_scale_v1_add_listener(st->fs, &fs_l, st);
		int so = ui_scale_override();
		if (so)
		{
			st->S = so < 120 ? 120 : so > 1200 ? 1200
											   : so;
			st->scale_locked = 1;
		}
	}
	set_limits(st);
	wl_surface_commit(st->surf);
	wl.display_roundtrip(st->dpy);
	wl.display_roundtrip(st->dpy);
	while (!st->configured && wl.display_dispatch(st->dpy) != -1)
	{
	}

	st->force = 1;
	draw(st);
	while (!ui_done(st->ctx) && wl.display_dispatch(st->dpy) != -1)
		post(st);

	teardown(st);
	return 0;
}
