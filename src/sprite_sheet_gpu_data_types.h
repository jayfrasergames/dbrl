#ifndef SPRITE_SHEET_GPU_DATA_TYPES_H
#define SPRITE_SHEET_GPU_DATA_TYPES_H

#include "jfg/cpu_gpu_data_types.h"

#define CLEAR_SPRITE_ID_WIDTH  8
#define CLEAR_SPRITE_ID_HEIGHT 8

struct Sprite_Sheet_Constant_Buffer
{
	F2 screen_size;
	F2 sprite_size;

	F2 tex_size;
	F2 _dummy;
};

struct Sprite_Sheet_Instance
{
	F2 sprite_pos;
	F2 world_pos;

	U1 sprite_id;
	F1 depth_offset;
};

#endif
