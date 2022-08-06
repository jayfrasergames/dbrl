#pragma once

#include "cpu_gpu_data_types.h"

struct Render_Sprites_Instance
{
	F2 source_top_left;
	F2 source_size;

	F2 dest_top_left;
	F2 dest_size;

	F4 color;
};