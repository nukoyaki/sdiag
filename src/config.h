#pragma once

#define TITLE_H 36	/* 34px bar + 2px separator, both inside section_top (extended) */
#define BOTTOM_H 40 /* 2px separator + 38px bar, both inside section_bottom (extended) */
#define TITLE_TEXT_X 16
#define TITLE_TEXT_Y 8
#define TITLE_TEXT_GAP 8 /* gap between the title text and the close box */

#define CLOSE_BOX 30
#define CLOSE_MARGIN 2
#define CLOSE_SPRITE_INSET 3

#define ICON_X 27
#define ICON_Y 29 /* from the top of the body */
#define TEXT_X 107
#define TEXT_Y 20 /* from the top of the body */
#define TEXT_PAD_BOTTOM 20
#define TEXT_RIGHT_MARGIN 33 /* scroll column (17) + gap (16), always reserved */

#define SCROLL_W 17
#define SCROLL_TRACK_PAD 2
#define FADE_H 24

#define BTN_H 30
#define BTN_Y 5 /* from the top of the bottom bar */
#define BTN_TEXT_Y 6
#define BTN_PAD_X 14
#define BTN_MIN_W 68
#define BTN_GAP 19
#define BTN_MARGIN 6 /* from the window edge, both sides */

#define WIN_MIN_W 300
#define WIN_MAX_W 500 /* the width is decided once, between these two, and then stays fixed */
#define WIN_MIN_H 176
#define WIN_MAX_H 900
#define INIT_MAX_H 333 /* initial height cap, the window can be resized taller by hand */
#define RESIZE_ZONE 3  /* top and bottom edges only */

#define FONT_W 10
#define FONT_H 19
#define LINE_H 19
#define TAB_COLS 4

#define MAX_BUTTONS 8
#define MAX_LINES 1000
#define DOUBLE_CLICK_MS 400
