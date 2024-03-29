#ifndef FIELD_OF_VISION_RENDER_GPU_DATA_TYPES_H
#define FIELD_OF_VISION_RENDER_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

#define FOV_SHADOW_BLEND_CS_WIDTH  8
#define FOV_SHADOW_BLEND_CS_HEIGHT 8

#define FOV_COMPOSITE_WIDTH  8
#define FOV_COMPOSITE_HEIGHT 8

#define FOV_CLEAR_WIDTH  8
#define FOV_CLEAR_HEIGHT 8

struct Field_Of_Vision_Render_Constant_Buffer
{
	F2 grid_size;
	F2 sprite_size;

	F2 edge_tex_size;
	U1 output_val;
	F1 _dummy;
};

struct Field_Of_Vision_Render_Blend_Constant_Buffer
{
	F1 alpha;
	F3 _dummy;
};

struct Field_Of_Vision_Edge_Instance
{
	F2 world_coords  SEMANTIC_NAME(WORLD_COORDS);
	F2 sprite_coords SEMANTIC_NAME(SPRITE_COORDS);
};

struct Field_Of_Vision_Fill_Instance
{
	F2 world_coords SEMANTIC_NAME(WORLD_COORDS);
};

#endif
