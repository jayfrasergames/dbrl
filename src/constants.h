#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "jfg/imgui.h"

struct Constants
{
#define BEGIN_SECTION(name) struct {
#define END_SECTION(name) } name;
#define F32(name, val) f32 name = val;
	#include "constants.edit.h"
#undef BEGIN_SECTION
#undef END_SECTION
#undef F32
};

Constants constants;

void constants_do_imgui(IMGUI_Context* imgui);

#ifndef JFG_HEADER_ONLY

void constants_do_imgui(IMGUI_Context* imgui)
{
	uptr cur_offset = (uptr)&constants;
#define BEGIN_SECTION(name) if (imgui_tree_begin(imgui, #name)) {
#define END_SECTION(name)   imgui_tree_end(imgui); }
#define F32(name, _val)     imgui_f32(imgui, #name, (f32*)cur_offset); cur_offset += sizeof(f32);
	#include "constants.edit.h"
#undef BEGIN_SECTION
#undef END_SECTION
#undef F32
}

#endif

#endif
