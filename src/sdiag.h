#pragma once

typedef enum {
    DIALOG_ICON_INFO = 0,
    DIALOG_ICON_WARNING,
    DIALOG_ICON_ERROR
} DialogIcon;

int native_dialog_show(
    const char* title, 
    const char* message, 
    const char** buttons, 
    int num_buttons, 
    DialogIcon icon
);