#ifndef JFG_IMGUI_GPU_DATA_TYPES_H
#define JFG_IMGUI_GPU_DATA_TYPES_H

#include "cpu_gpu_data_types.h"

struct IMGUI_CB_Text
{
	F2 screen_size;
	F2 glyph_size;

	F2 tex_size;
	F1 zoom;
	F1 _dummy;
};

struct IMGUI_VS_Text_Instance
{
	U1 glyph;
	F2 pos;
	F4 color;
};

#endif
