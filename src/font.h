#pragma once
#include <stdint.h>

/* returns the glyph bitmap for a codepoint, or NULL for blank cells; bit order is row-major, MSB first */
const uint8_t *font_glyph(uint32_t cp);
