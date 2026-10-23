package sdiag

import "core:c"
import "core:strings"

foreign import lib {
	"build/libsdiag.a",
}

DialogIcon :: enum c.int { Info, Warning, Error }

@(default_calling_convention = "c")
@(private = "file")
foreign lib {
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
show :: proc(title, message: string, buttons: []string, icon := DialogIcon.Info, temp_allocator := context.temp_allocator) -> int {
	cbuttons := make([]cstring, len(buttons), temp_allocator)

	for b, i in buttons {
		cbuttons[i] = strings.clone_to_cstring(b, temp_allocator)
	}

	return int(sdiag_show(
		strings.clone_to_cstring(title, temp_allocator),
		strings.clone_to_cstring(message, temp_allocator),
		raw_data(cbuttons), c.int(len(cbuttons)), icon,
	))
}