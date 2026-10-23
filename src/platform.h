#pragma once

/* each returns 0 if the dialog was shown (result is in ui_result), nonzero if the backend is unavailable */
int plat_run_x11(void);
int plat_run_wayland(void);
