#include "sdiag.h"

int sdiag_show(
	const char *title,
	const char *message,
	const char **buttons,
	int num_buttons,
	DialogIcon icon)
{
	return 0;
}

pixel_t g_bg_color = {}; // 48, 64 : 1x1
pixel_t g_bg2_color = {}; // 49, 64 : 1x1
pixel_t g_text_color = {}; // 50, 64 : 1x1
pixel_t g_bg_sel_color = {}; // 51, 64 : 1x1
pixel_t g_text_sel_color = {}; // 52, 64 : 1x1

sprite_t g_spr_info;	 // 0, 0 : 64x64
sprite_t g_spr_warning; // 64, 0 : 64x64
sprite_t g_spr_error;	 // 128, 0 : 64x64

sprite_t g_spr_close; // 0, 64 : 24x24

sprite_t g_spr_emboss_tl; // 24, 64 : 12x12
sprite_t g_spr_emboss_tr; // 36, 64 : 12x12
sprite_t g_spr_emboss_bl; // 24, 76 : 12x12
sprite_t g_spr_emboss_br; // 36, 76 : 12x12

sprite_t g_spr_emboss_hov_tl; // 114, 64 : 12x12
sprite_t g_spr_emboss_hov_tr; // 156, 64 : 12x12
sprite_t g_spr_emboss_hov_bl; // 114, 76 : 12x12
sprite_t g_spr_emboss_hov_br; // 156, 76 : 12x12

sprite_t g_spr_emboss_down_tl; // 168, 64 : 12x12
sprite_t g_spr_emboss_down_tr; // 180, 64 : 12x12
sprite_t g_spr_emboss_down_bl; // 168, 76 : 12x12
sprite_t g_spr_emboss_down_br; // 180, 76 : 12x12

sprite_t g_spr_section_top;		  // 72, 64 : 24x24
sprite_t g_spr_section_bottom;		  // 96, 64 : 24x24
sprite_t g_spr_section_scroll_piece_start; // 120, 66 : 17x4
sprite_t g_spr_section_scroll_piece_middle; // 120, 72 : 17x12
sprite_t g_spr_section_scroll_piece_end; // 120, 84 : 17x4

int init_sdiag()
{
	
}