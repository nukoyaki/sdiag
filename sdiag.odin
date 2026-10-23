package sdiag

import "core:c"
import "core:strings"
import "core:sync"

foreign import lib "build/libsdiag.a"

DialogIcon :: enum c.int {
	Info,
	Warning,
	Error,
}

@(private = "file")
init_mutex: sync.Mutex
@(private = "file")
initialized: bool

@(default_calling_convention = "c")
@(private = "file")
foreign lib {
	sdiag_init :: proc() -> c.int ---
	sdiag_show :: proc(title, message: cstring, buttons: [^]cstring, num_buttons: c.int, icon: DialogIcon) -> c.int ---
}

/*
	show a dialog via sdiag

	Inputs:
	- `title`: the title of the dialog
	- `message`: the body of the dialog
	- `buttons`: the buttons for the dialog, ordered from right to left
	- `icon`: the icon of the dialog
	- `temp_allocator`: the allocator to use for cloned cstrings, these don't get freed so you likely want this to be left as default
	Returns:
	the pressed button index or -1 if closed or -2 if the dialog couldn't be displayed
*/
show :: proc(
	title, message: string,
	buttons: []string,
	icon := DialogIcon.Info,
	temp_allocator := context.temp_allocator,
) -> int {
	sync.lock(&init_mutex)
	if !initialized {
		initialized = sdiag_init() != 0
	}
	ok := initialized
	sync.unlock(&init_mutex)
	if !ok {
		return -2
	}

	cbuttons := make([]cstring, len(buttons), temp_allocator)

	for b, i in buttons {
		cbuttons[i] = strings.clone_to_cstring(b, temp_allocator)
	}

	return int(
		sdiag_show(
			strings.clone_to_cstring(title, temp_allocator),
			strings.clone_to_cstring(message, temp_allocator),
			raw_data(cbuttons),
			c.int(len(cbuttons)),
			icon,
		),
	)
}
