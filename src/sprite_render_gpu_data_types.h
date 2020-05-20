#ifndef SPRITE_RENDER_GPU_DATA_TYPES_H
#define SPRITE_RENDER_GPU_DATA_TYPES_H

#include "jfg/cpu_gpu_data_types.h"

struct Sprite_Render_CB_Sprite
{
	F2 screen_size;
	F2 sprite_size;

	F2 tex_size;
	F1 zoom;
	F1 _dummy;
};

struct Sprite_Render_Sprite_Instance
{
	F2 sprite_pos;
	F2 world_pos;
};

#endif
