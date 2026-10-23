#pragma once

typedef enum
{
	DIALOG_ICON_INFO = 0,
	DIALOG_ICON_WARNING,
	DIALOG_ICON_ERROR
} DialogIcon;

/* returns the index of the pressed button, -1 if the dialog was closed (esc or the close button),
   or -2 if it could not be shown (no display, missing libraries, etc) */
int sdiag_show(
	const char *title,
	const char *message,
	const char **buttons,
	int num_buttons,
	DialogIcon icon);
