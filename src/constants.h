#pragma once

#include "imgui.h"

struct Constants
{
#define BEGIN_SECTION(name) struct {
#define END_SECTION(name) } name;
#define F32(name, val, _min, _max) f32 name = val;
#define U32(name, val, _min, _max) u32 name = val;
	#include "constants.edit.h"
#undef BEGIN_SECTION
#undef END_SECTION
#undef F32
#undef U32
};

extern Constants constants;

void constants_do_imgui(IMGUI_Context* imgui);