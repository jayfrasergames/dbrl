#ifndef CARD_RENDER_GPU_DATA_TYPES_H
#define CARD_RENDER_GPU_DATA_TYPES_H

#include "jfg/cpu_gpu_data_types.h"

struct Card_Render_Constant_Buffer
{
	F2 card_size;
	F2 screen_size;

	F2 tex_size;
	F1 zoom;
	F1 _dummy;
};

struct Card_Render_Instance
{
	F1 horizontal_rotation;
	F1 vertical_rotation;
	F1 screen_rotation;
	F1 z_offset; // not used in shader

	F2 screen_pos;
	F2 card_pos;
};

#endif
