#ifndef JFG_DEBUG_LINE_GPU_DATA_TYPES_H
#define JFG_DEBUG_LINE_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

struct Debug_Line_Constant_Buffer
{
	F2 top_left;
	F2 bottom_right;
};

struct Debug_Line_Instance
{
	F2 start;
	F2 end;

	F4 color;
};

#endif
