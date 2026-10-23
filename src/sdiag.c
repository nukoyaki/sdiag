#include <stdlib.h>
#include "platform.h"
#include "sdiag.h"
#include "ui.h"

#define SD_API __attribute__((visibility("default")))

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
	if (!ui_init(title, message ? message : "", buttons, num_buttons, icon))
		return -2;

	int shown = 0;
	if (getenv("WAYLAND_DISPLAY"))
		shown = plat_run_wayland() == 0;
	if (!shown && getenv("DISPLAY"))
		shown = plat_run_x11() == 0;

	int r = shown ? ui_result() : -2;
	ui_free();
	return r;
}
