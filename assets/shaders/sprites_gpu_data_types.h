#ifndef SPRITE_GPU_DATA_TYPES_H
#define SPRITE_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

struct Sprite_Constants
{
	F2 tile_output_size;
	F2 tile_input_size;
};

struct Sprite_Instance
{
	F2 glyph_coords  SEMANTIC_NAME(GLYPH_COORDS);
	F2 output_coords SEMANTIC_NAME(OUTPUT_COORDS);
	F4 color         SEMANTIC_NAME(COLOR);
};

#endif