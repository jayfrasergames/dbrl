#include "constants.h"

Constants constants;

#define BEGIN_SECTION(_name)       + 1
#define END_SECTION(_name)         + 1
#define F32(name, val, _min, _max) + 1
#define U32(name, val, _min, _max) + 1
	const u32 constants_num_lines = 0
	#include "constants.edit.h"
	;
#undef BEGIN_SECTION
#undef END_SECTION
#undef F32
#undef U32

uptr constants_offsets[constants_num_lines];

void constants_do_imgui(IMGUI_Context* imgui)
{
	uptr cur_offset = 0;

#define BEGIN_SECTION(_name)
#define END_SECTION(_name)
#define F32(name, val, _min, _max) constants_offsets[__LINE__] = cur_offset; cur_offset += sizeof(f32);
#define U32(name, val, _min, _max) constants_offsets[__LINE__] = cur_offset; cur_offset += sizeof(u32);
	#include "constants.edit.h"
#undef BEGIN_SECTION
#undef END_SECTION
#undef F32
#undef U32

	uptr base = (uptr)&constants;
	cur_offset = 0;
#define BEGIN_SECTION(name) if (imgui_tree_begin(imgui, #name)) {
#define END_SECTION(name)   imgui_tree_end(imgui); }
#define F32(name, _val, min_val, max_val) \
		imgui_f32(imgui, #name, (f32*)(base + constants_offsets[__LINE__]), min_val, max_val);
#define U32(name, _val, min_val, max_val) \
		imgui_u32(imgui, #name, (u32*)(base + constants_offsets[__LINE__]), min_val, max_val);
	#include "constants.edit.h"
#undef BEGIN_SECTION
#undef END_SECTION
#undef F32
}
