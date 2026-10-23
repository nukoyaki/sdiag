#include <stdlib.h>
#include "atlas.h"
#include "platform.h"
#include "sdiag.h"
#include "ui.h"

#define SD_API __attribute__((visibility("default")))

SD_API int sdiag_init(void)
{
	if (!atlas_init())
		return 0;
	plat_load_wayland();
	plat_load_x11();
	return 1;
}

SD_API int sdiag_show(
	const char *title,
	const char *message,
	const char **buttons,
	int num_buttons,
	DialogIcon icon)
{
	static const char *ok[] = {"Ok"};
	if (!buttons || num_buttons < 1)
	{
		buttons = ok;
		num_buttons = 1;
	}
	diag_ctx_t *ctx = malloc(sizeof *ctx);
	if (!ctx)
		return -2;
	if (!ui_init(ctx, title, message ? message : "", buttons, num_buttons, icon))
	{
		free(ctx);
		return -2;
	}

	int shown = 0;
	if (getenv("WAYLAND_DISPLAY"))
		shown = plat_run_wayland(ctx) == 0;
	if (!shown && getenv("DISPLAY"))
		shown = plat_run_x11(ctx) == 0;

	int r = shown ? ui_result(ctx) : -2;
	ui_free(ctx);
	free(ctx);
	return r;
}
