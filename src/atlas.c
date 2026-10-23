#include <stdlib.h>
#include <string.h>
#include "atlas.h"

#define qoi_encode sd_qoi_encode
#define qoi_decode sd_qoi_decode
#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include "qoi.h"

static const unsigned char atlas_qoi[] = {
#embed "../resources/icons.qoi"
};

pixel_t g_bg_color;		  /* 48, 64 : 1x1 */
pixel_t g_bg2_color;	  /* 49, 64 : 1x1 */
pixel_t g_text_color;	  /* 50, 64 : 1x1 */
pixel_t g_bg_sel_color;	  /* 51, 64 : 1x1 */
pixel_t g_text_sel_color; /* 52, 64 : 1x1 */

sprite_t g_spr_info;	/* 0, 0 : 64x64 */
sprite_t g_spr_warning; /* 64, 0 : 64x64 */
sprite_t g_spr_error;	/* 128, 0 : 64x64 */

sprite_t g_spr_close; /* 0, 64 : 24x24 */

sprite_t g_spr_emboss_tl; /* 24, 64 : 12x12 */
sprite_t g_spr_emboss_tr; /* 36, 64 : 12x12 */
sprite_t g_spr_emboss_bl; /* 24, 76 : 12x12 */
sprite_t g_spr_emboss_br; /* 36, 76 : 12x12 */

sprite_t g_spr_emboss_hov_tl; /* 144, 64 : 12x12 */
sprite_t g_spr_emboss_hov_tr; /* 156, 64 : 12x12 */
sprite_t g_spr_emboss_hov_bl; /* 144, 76 : 12x12 */
sprite_t g_spr_emboss_hov_br; /* 156, 76 : 12x12 */

sprite_t g_spr_emboss_down_tl; /* 168, 64 : 12x12 */
sprite_t g_spr_emboss_down_tr; /* 180, 64 : 12x12 */
sprite_t g_spr_emboss_down_bl; /* 168, 76 : 12x12 */
sprite_t g_spr_emboss_down_br; /* 180, 76 : 12x12 */

sprite_t g_spr_section_top;	   /* 72, 64 : 24x24 */
sprite_t g_spr_section_bottom; /* 96, 64 : 24x24 */

sprite_t g_spr_section_scroll_piece_start;	/* 120, 66 : 17x6 */
sprite_t g_spr_section_scroll_piece_middle; /* 120, 72 : 17x12 */
sprite_t g_spr_section_scroll_piece_end;	/* 120, 84 : 17x4 */

static const struct
{
	sprite_t *s;
	int x, y, w, h;
} cuts[] = {
	{&g_spr_info, 0, 0, 64, 64},
	{&g_spr_warning, 64, 0, 64, 64},
	{&g_spr_error, 128, 0, 64, 64},
	{&g_spr_close, 0, 64, 24, 24},
	{&g_spr_emboss_tl, 24, 64, 12, 12},
	{&g_spr_emboss_tr, 36, 64, 12, 12},
	{&g_spr_emboss_bl, 24, 76, 12, 12},
	{&g_spr_emboss_br, 36, 76, 12, 12},
	{&g_spr_emboss_hov_tl, 144, 64, 12, 12},
	{&g_spr_emboss_hov_tr, 156, 64, 12, 12},
	{&g_spr_emboss_hov_bl, 144, 76, 12, 12},
	{&g_spr_emboss_hov_br, 156, 76, 12, 12},
	{&g_spr_emboss_down_tl, 168, 64, 12, 12},
	{&g_spr_emboss_down_tr, 180, 64, 12, 12},
	{&g_spr_emboss_down_bl, 168, 76, 12, 12},
	{&g_spr_emboss_down_br, 180, 76, 12, 12},
	{&g_spr_section_top, 72, 64, 24, 24},
	{&g_spr_section_bottom, 96, 64, 24, 24},
	{&g_spr_section_scroll_piece_start, 120, 66, 17, 6},
	{&g_spr_section_scroll_piece_middle, 120, 72, 17, 12},
	{&g_spr_section_scroll_piece_end, 120, 84, 17, 4},
};

static const struct
{
	pixel_t *p;
	int x, y;
} samples[] = {
	{&g_bg_color, 48, 64},
	{&g_bg2_color, 49, 64},
	{&g_text_color, 50, 64},
	{&g_bg_sel_color, 51, 64},
	{&g_text_sel_color, 52, 64},
};

static pixel_t *pool;

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

int atlas_init(void)
{
	qoi_desc d;
	pixel_t *src = qoi_decode(atlas_qoi, (int)sizeof atlas_qoi, &d, 3);
	if (!src)
		return 0;

	size_t total = 0;
	for (int i = 0; i < COUNT(cuts); i++)
	{
		if (cuts[i].x + cuts[i].w > (int)d.width || cuts[i].y + cuts[i].h > (int)d.height)
		{
			free(src);
			return 0;
		}
		total += (size_t)cuts[i].w * cuts[i].h;
	}
	for (int i = 0; i < COUNT(samples); i++)
		if (samples[i].x >= (int)d.width || samples[i].y >= (int)d.height)
		{
			free(src);
			return 0;
		}

	pool = malloc(total * sizeof *pool);
	if (!pool)
	{
		free(src);
		return 0;
	}

	pixel_t *at = pool;
	for (int i = 0; i < COUNT(cuts); i++)
	{
		sprite_t *s = cuts[i].s;
		s->width = (unsigned short)cuts[i].w;
		s->height = (unsigned short)cuts[i].h;
		s->data = at;
		for (int y = 0; y < cuts[i].h; y++, at += cuts[i].w)
			memcpy(at, src + (size_t)(cuts[i].y + y) * d.width + cuts[i].x, (size_t)cuts[i].w * sizeof *at);
	}
	for (int i = 0; i < COUNT(samples); i++)
		*samples[i].p = src[(size_t)samples[i].y * d.width + samples[i].x];

	free(src);
	return 1;
}

void atlas_free(void)
{
	free(pool);
	pool = NULL;
}
