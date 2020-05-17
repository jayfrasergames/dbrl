#ifndef TILE_RENDER_GPU_DATA_TYPES_H
#define TILE_RENDER_GPU_DATA_TYPES_H

#include "jfg/cpu_gpu_data_types.h"

struct Tile_Render_CB_Tile
{
	F2 screen_size;
	F2 tile_size;

	F2 tex_size;
	F1 zoom;
	F1 _dummy;
};

struct Tile_Render_Tile_Instance
{
	F2 sprite_pos;
	F2 world_pos;
};

#endif
