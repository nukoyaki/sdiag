#pragma once
#include "ui.h"

/* loads the backend's library, called once from sdiag_init; returns 1 if the backend is usable */
int plat_load_x11(void);
int plat_load_wayland(void);

/* each returns 0 if the dialog was shown (result is in ui_result), nonzero if the backend is unavailable */
int plat_run_x11(diag_ctx_t *ctx);
int plat_run_wayland(diag_ctx_t *ctx);
