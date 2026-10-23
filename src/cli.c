#include <stdio.h>
#include <string.h>
#include "sdiag.h"

static int usage(void)
{
	/* we use 254 instead of EX_USAGE from sysexits.h as to keep the failure exit codes as far away from response exit codes as possible */
	fputs("usage: sdiag [-t title] [-i info|warning|error] message [button...]\n"
		  "exit code: index of the pressed button, 255 if closed or no display, 254 on bad arguments\n",
		  stderr);
	return 254;
}

int main(int argc, char **argv)
{
	const char *title = "sdiag";
	DialogIcon icon = DIALOG_ICON_INFO;
	int i = 1;
	for (; i < argc; i++)
	{
		if (!strcmp(argv[i], "-t") && i + 1 < argc)
			title = argv[++i];
		else if (!strcmp(argv[i], "-i") && i + 1 < argc)
		{
			const char *n = argv[++i];
			if (!strcmp(n, "info"))
				icon = DIALOG_ICON_INFO;
			else if (!strcmp(n, "warning"))
				icon = DIALOG_ICON_WARNING;
			else if (!strcmp(n, "error"))
				icon = DIALOG_ICON_ERROR;
			else
				return usage();
		}
		else if (!strcmp(argv[i], "--"))
		{
			i++;
			break;
		}
		else if (argv[i][0] == '-' && argv[i][1])
			return usage();
		else
			break;
	}
	if (i >= argc)
		return usage();
	const char *message = argv[i++];
	if (!sdiag_init())
		return 255;
	int r = sdiag_show(title, message, (const char **)(argv + i), argc - i, icon);
	return r < 0 ? 255 : r;
}
