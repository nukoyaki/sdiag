#pragma once
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "paint.h"
#include "types.h"

#define EDGE_T 1
#define EDGE_B 2

#define CUR_ARROW 0
#define CUR_TEXT 1
#define CUR_RESIZE_T 2
#define CUR_RESIZE_B 3

#define ACT_NONE 0
#define ACT_MOVE 1
#define ACT_RESIZE 2

typedef struct line_s
{
	int start, end, nl;
} line_t;

typedef struct diag_ctx_s
{
	sprite_t screen, title_sprite, body_sprite;
	paint_t paint;

	const char *title, *message;
	const char *const *btn;
	int nbtn, icon, tcells, bcells[MAX_BUTTONS];
	uint32_t *cps, *bcp[MAX_BUTTONS];
	int ncps, nshown, maxcells;
	line_t *lines;
	int nlines, cols;

	int W, H, vh, total, maxscroll, scroll, sbar;
	rect_t dirty, btn_r[MAX_BUTTONS], close_r, thumb_r, track_r;

	int mx, my, hover_btn, hover_close, focus, down, down_i, dragoff;
	int sel_a, sel_b, ctrl, shift;
	int last_valid, last_x, last_y;
	unsigned last_t;

	int done, result, action, edge, copy;
	char *copybuf;
	size_t copylen;
} diag_ctx_t;

int ui_init(diag_ctx_t *ctx, const char *title, const char *message, const char *const *buttons, int n, int icon);
void ui_free(diag_ctx_t *ctx);
const char *ui_title(diag_ctx_t *ctx);

void ui_size(diag_ctx_t *ctx, int *w, int *h);
void ui_limits(int *minh, int *maxh);
void ui_set_height(diag_ctx_t *ctx, int h);
void ui_invalidate(diag_ctx_t *ctx);
int ui_paint(diag_ctx_t *ctx, rect_t *dirty);
void ui_present(diag_ctx_t *ctx, uint32_t *out, int ow, int oh, int scale120, const rect_t *src_dirty, rect_t *dst_dirty);

void ui_pointer(diag_ctx_t *ctx, int x, int y);
void ui_leave(diag_ctx_t *ctx);
void ui_button(diag_ctx_t *ctx, int pressed, unsigned time);
void ui_wheel(diag_ctx_t *ctx, int lines_x100);
void ui_key(diag_ctx_t *ctx, int evdev, int down);
void ui_focus_lost(diag_ctx_t *ctx);

int ui_done(diag_ctx_t *ctx);
int ui_result(diag_ctx_t *ctx);
int ui_take_action(diag_ctx_t *ctx, int *edge);
const char *ui_take_copy(diag_ctx_t *ctx, size_t *len);
int ui_cursor(diag_ctx_t *ctx);

int ui_scale_override(void);
int ui_scale_env(void);
