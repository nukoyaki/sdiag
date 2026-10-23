#pragma once
#include "types.h"

extern pixel_t g_bg_color, g_bg2_color, g_text_color, g_bg_sel_color, g_text_sel_color;

extern sprite_t g_spr_info, g_spr_warning, g_spr_error;
extern sprite_t g_spr_close;

extern sprite_t g_spr_emboss_tl, g_spr_emboss_tr, g_spr_emboss_bl, g_spr_emboss_br;
extern sprite_t g_spr_emboss_hov_tl, g_spr_emboss_hov_tr, g_spr_emboss_hov_bl, g_spr_emboss_hov_br;
extern sprite_t g_spr_emboss_down_tl, g_spr_emboss_down_tr, g_spr_emboss_down_bl, g_spr_emboss_down_br;

extern sprite_t g_spr_section_top, g_spr_section_bottom;
extern sprite_t g_spr_section_scroll_piece_start, g_spr_section_scroll_piece_middle, g_spr_section_scroll_piece_end;

int atlas_init(void);
void atlas_free(void);
