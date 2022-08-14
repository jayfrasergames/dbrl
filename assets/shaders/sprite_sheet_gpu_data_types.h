#ifndef SPRITE_SHEET_GPU_DATA_TYPES_H
#define SPRITE_SHEET_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

#define CLEAR_SPRITE_ID_WIDTH  8
#define CLEAR_SPRITE_ID_HEIGHT 8

#define HIGHLIGHT_SPRITE_WIDTH  8
#define HIGHLIGHT_SPRITE_HEIGHT 8

struct Sprite_Sheet_Constant_Buffer
{
	F2 screen_size;
	F2 sprite_size;

	F2 world_tile_size;
	F2 tex_size;
};

struct Sprite_Sheet_Instance
{
	F2 sprite_pos    SEMANTIC_NAME(SPRITE_POS);
	F2 world_pos     SEMANTIC_NAME(WORLD_POS);

	U1 sprite_id     SEMANTIC_NAME(SPRITE_ID);
	F1 depth_offset  SEMANTIC_NAME(DEPTH_OFFSET);
	F1 y_offset      SEMANTIC_NAME(Y_OFFSET);

	F4 color_mod     SEMANTIC_NAME(COLOR_MOD);
};

struct Sprite_Sheet_Font_Instance
{
	F2 glyph_pos     SEMANTIC_NAME(GLYPH_POS);
	F2 glyph_size    SEMANTIC_NAME(GLYPH_SIZE);

	F2 world_pos     SEMANTIC_NAME(WORLD_POS);
	F2 world_offset  SEMANTIC_NAME(WORLD_OFFSET);

	F1 zoom          SEMANTIC_NAME(ZOOM);

	F4 color_mod     SEMANTIC_NAME(COLOR_MOD);
};

struct Sprite_Sheet_Highlight_Constant_Buffer
{
	F4 highlight_color;

	U1 sprite_id;
	U1 _dummy0;
	U2 _dummy1;
};

#endif
