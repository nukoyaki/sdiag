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
		F(display_dispatch, "wl_display_dispatch") F(display_roundtrip, "wl_display_roundtrip")
			F(display_flush, "wl_display_flush") F(marshal_array_flags, "wl_proxy_marshal_array_flags")
				F(add_listener, "wl_proxy_add_listener") F(destroy, "wl_proxy_destroy")
					F(get_version, "wl_proxy_get_version") F(set_user_data, "wl_proxy_set_user_data")
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

static struct wl_display *dpy;
static struct wl_registry *reg;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_data_device_manager *ddm;
static struct wl_data_device *ddev;
static struct wl_data_source *dsrc;
static struct wp_fractional_scale_manager_v1 *fsm;
static struct wp_viewporter *vpr;
static struct wp_cursor_shape_manager_v1 *csm;
static struct wl_surface *surf;
static struct xdg_surface *xs;
static struct xdg_toplevel *top;
static struct wp_viewport *vp;
static struct wp_fractional_scale_v1 *fs;
static struct wl_pointer *ptr;
static struct wl_keyboard *kbd;
static struct wp_cursor_shape_device_v1 *csd;

static int S = 120, scale_locked, configured, force, cfg_h, last_cur = -1;
static uint32_t enter_serial, btn_serial, key_serial;
static char *clip;
static size_t cliplen;

static void set_limits(void)
{
	int w, h, minh, maxh;
	ui_size(&w, &h);
	ui_limits(&minh, &maxh);
	xdg_toplevel_set_min_size(top, w, minh);
	xdg_toplevel_set_max_size(top, w, maxh);
}

static void buf_release(void *d, struct wl_buffer *b)
{
	(void)d;
	wl_buffer_destroy(b);
}
static const struct wl_buffer_listener buf_l = {buf_release};

static void draw(void)
{
	rect_t sd, dd;
	int painted = ui_paint(&sd);
	if (!painted && !force)
		return;
	force = 0;

	int w, h;
	ui_size(&w, &h);
	int ow = (w * S + 60) / 120, oh = (h * S + 60) / 120;
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
	ui_present(m, ow, oh, S, NULL, &dd);
	munmap(m, sz);

	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)sz);
	struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, ow, oh, ow * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b, &buf_l, NULL);

	if (vp)
		wp_viewport_set_destination(vp, w, h);
	wl_surface_attach(surf, b, 0, 0);
	if (wl_surface_get_version(surf) >= 4)
		wl_surface_damage_buffer(surf, 0, 0, ow, oh);
	else
		wl_surface_damage(surf, 0, 0, w, h);
	wl_surface_commit(surf);
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
	(void)d;
	(void)p;
	(void)s;
	enter_serial = serial;
	last_cur = -1;
	ui_pointer(wl_fixed_to_int(x), wl_fixed_to_int(y));
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s)
{
	(void)d;
	(void)p;
	(void)serial;
	(void)s;
	ui_leave();
}
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d;
	(void)p;
	(void)t;
	ui_pointer(wl_fixed_to_int(x), wl_fixed_to_int(y));
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t b, uint32_t st)
{
	(void)d;
	(void)p;
	if (b != 0x110)
		return;
	btn_serial = serial;
	ui_button(st == WL_POINTER_BUTTON_STATE_PRESSED, t);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	(void)d;
	(void)p;
	(void)t;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		ui_wheel((int)(wl_fixed_to_double(v) * 30));
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
	(void)d;
	(void)k;
	(void)s;
	(void)sf;
	ui_focus_lost();
}
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t st)
{
	(void)d;
	(void)k;
	(void)t;
	key_serial = serial;
	ui_key((int)key, st == WL_KEYBOARD_KEY_STATE_PRESSED);
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
	(void)d;
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ptr)
	{
		ptr = wl_seat_get_pointer(s);
		wl_pointer_add_listener(ptr, &ptr_l, NULL);
		if (csm)
			csd = wp_cursor_shape_manager_v1_get_pointer(csm, ptr);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !kbd)
	{
		kbd = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(kbd, &kbd_l, NULL);
	}
}
static const struct wl_seat_listener seat_l = {.capabilities = seat_caps, .name = seat_name};

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	(void)d;
	if (!strcmp(iface, wl_compositor_interface.name))
		comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, xdg_wm_base_interface.name))
	{
		wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm, &wm_l, NULL);
	}
	else if (!strcmp(iface, wl_seat_interface.name) && !seat)
	{
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_l, NULL);
	}
	else if (!strcmp(iface, wl_data_device_manager_interface.name))
		ddm = wl_registry_bind(r, name, &wl_data_device_manager_interface, 1);
	else if (!strcmp(iface, wp_fractional_scale_manager_v1_interface.name))
		fsm = wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1);
	else if (!strcmp(iface, wp_viewporter_interface.name))
		vpr = wl_registry_bind(r, name, &wp_viewporter_interface, 1);
	else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
		csm = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
}
static const struct wl_registry_listener reg_l = {reg_global, reg_remove};

static void xs_configure(void *d, struct xdg_surface *s, uint32_t serial)
{
	(void)d;
	xdg_surface_ack_configure(s, serial);
	configured = 1;
	int w, h;
	ui_size(&w, &h);
	if (cfg_h > 0 && cfg_h != h)
	{
		ui_set_height(cfg_h);
		force = 1;
	}
}
static const struct xdg_surface_listener xs_l = {xs_configure};

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	(void)d;
	(void)t;
	(void)w;
	(void)st;
	cfg_h = h;
}
static void top_close(void *d, struct xdg_toplevel *t)
{
	(void)d;
	(void)t;
	ui_key(1, 1);
}
static const struct xdg_toplevel_listener top_l = {.configure = top_configure, .close = top_close};

static void fs_scale(void *d, struct wp_fractional_scale_v1 *f, uint32_t s)
{
	(void)d;
	(void)f;
	if (scale_locked)
		return;
	S = s < 120 ? 120 : s > 1200 ? 1200
								 : (int)s;
	force = 1;
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
	(void)d;
	(void)s;
	(void)mime;
	for (size_t o = 0; o < cliplen;)
	{
		ssize_t n = write(fd, clip + o, cliplen - o);
		if (n <= 0)
			break;
		o += (size_t)n;
	}
	close(fd);
}
static void ds_cancelled(void *d, struct wl_data_source *s)
{
	(void)d;
	wl_data_source_destroy(s);
	if (s == dsrc)
		dsrc = NULL;
}
static const struct wl_data_source_listener ds_l = {.target = ds_target, .send = ds_send, .cancelled = ds_cancelled};

static void set_clipboard(const char *t, size_t n)
{
	if (!ddm || !ddev)
		return;
	free(clip);
	clip = malloc(n + 1);
	if (!clip)
		return;
	memcpy(clip, t, n);
	cliplen = n;
	struct wl_data_source *s = wl_data_device_manager_create_data_source(ddm);
	wl_data_source_add_listener(s, &ds_l, NULL);
	wl_data_source_offer(s, "text/plain;charset=utf-8");
	wl_data_source_offer(s, "text/plain");
	wl_data_source_offer(s, "UTF8_STRING");
	wl_data_device_set_selection(ddev, s, key_serial);
	dsrc = s;
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

static void post(void)
{
	int edge, a = ui_take_action(&edge);
	if (a == ACT_MOVE && seat)
		xdg_toplevel_move(top, seat, btn_serial);
	else if (a == ACT_RESIZE && seat)
		xdg_toplevel_resize(top, seat, btn_serial, edge == EDGE_T ? XDG_TOPLEVEL_RESIZE_EDGE_TOP : XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
	size_t n;
	const char *t = ui_take_copy(&n);
	if (t)
		set_clipboard(t, n);
	int cur = ui_cursor();
	if (csd && cur != last_cur)
	{
		wp_cursor_shape_device_v1_set_shape(csd, enter_serial, shape_for(cur));
		last_cur = cur;
	}
	draw();
}

int plat_run_wayland(void)
{
	if (!load())
		return 1;
	dpy = wl.display_connect(NULL);
	if (!dpy)
		return 1;

	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_l, NULL);
	wl.display_roundtrip(dpy);
	if (!comp || !shm || !wm)
	{
		wl.display_disconnect(dpy);
		return 1;
	}
	if (seat && ddm)
		ddev = wl_data_device_manager_get_data_device(ddm, seat);

	surf = wl_compositor_create_surface(comp);
	xs = xdg_wm_base_get_xdg_surface(wm, surf);
	xdg_surface_add_listener(xs, &xs_l, NULL);
	top = xdg_surface_get_toplevel(xs);
	xdg_toplevel_add_listener(top, &top_l, NULL);
	xdg_toplevel_set_title(top, ui_title());
	xdg_toplevel_set_app_id(top, "sdiag");
	if (fsm && vpr)
	{
		vp = wp_viewporter_get_viewport(vpr, surf);
		fs = wp_fractional_scale_manager_v1_get_fractional_scale(fsm, surf);
		wp_fractional_scale_v1_add_listener(fs, &fs_l, NULL);
		int so = ui_scale_override();
		if (so)
		{
			S = so < 120 ? 120 : so > 1200 ? 1200
										   : so;
			scale_locked = 1;
		}
	}
	set_limits();
	wl_surface_commit(surf);
	wl.display_roundtrip(dpy);
	wl.display_roundtrip(dpy);
	while (!configured && wl.display_dispatch(dpy) != -1)
	{
	}

	force = 1;
	draw();
	while (!ui_done() && wl.display_dispatch(dpy) != -1)
		post();

	wl.display_flush(dpy);
	wl.display_disconnect(dpy);
	free(clip);
	clip = NULL;
	return 0;
}
