#ifndef DEBUG_DRAW_WORLD_GPU_DATA_TYPES_H
#define DEBUG_DRAW_WORLD_GPU_DATA_TYPES_H

#include "jfg/cpu_gpu_data_types.h"

struct Debug_Draw_World_Constant_Buffer
{
	F2 center;
	F2 zoom;
};

struct Debug_Draw_World_Triangle
{
	F2 a;
	F2 b;

	F2 c;
	F4 color; // Nvidia will be upset :(
};

#endif
