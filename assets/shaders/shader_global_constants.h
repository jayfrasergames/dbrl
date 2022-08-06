#ifndef SHADER_GLOBAL_CONSTANTS_H
#define SHADER_GLOBAL_CONSTANTS_H

#include "cpu_gpu_data_types.h"

struct Shader_Global_Constants
{
	F2 screen_size;
};

struct Shader_Per_Dispatch_Constants
{
	U1 base_offset;
};

#endif