#ifndef DEBUG_DRAW_WORLD_GPU_DATA_TYPES_H
#define DEBUG_DRAW_WORLD_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

struct Debug_Draw_World_Constant_Buffer
{
	F2 center;
	F2 zoom;
};

struct Debug_Draw_World_Triangle
{
	F2 a        SEMANTIC_NAME(VERTEX_A);
	F2 b        SEMANTIC_NAME(VERTEX_B);

	F2 c        SEMANTIC_NAME(VERTEX_C);
	F4 color    SEMANTIC_NAME(COLOR); // Nvidia will be upset :(
};

struct Debug_Draw_World_Line
{
	F2 start  SEMANTIC_NAME(START);
	F2 end    SEMANTIC_NAME(END);

	F4 color  SEMANTIC_NAME(COLOR);
};

#endif
