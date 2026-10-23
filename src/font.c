#include "config.h"
#include "font.h"

static const uint8_t font_raw[] = {
	/* my IDE whines that this cannot be read but it is delusional because this file exists and this compiles fine
	so ignore it */
#embed "../resources/font.raw"
};

#define GLYPH_BYTES ((FONT_W * FONT_H + 7) / 8)

const uint8_t *font_glyph(uint32_t cp)
{
	if (cp <= ' ')
		return 0;
	if (cp > '~')
		cp = '?';

	return font_raw + (cp - '!') * GLYPH_BYTES;
}
