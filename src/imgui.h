#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "render.h"

#include "imgui_gpu_data_types.h"
#include "input.h"

#define IMGUI_MAX_TEXT_CHARACTERS 4096
#define IMGUI_MAX_ELEMENTS 4096

struct IMGUI_Element_State
{
	uptr id;
	union {
		struct {
			u8 collapsed;
		} tree_begin;
	};
};

struct IMGUI_Context
{
	v2 text_pos;
	v4 text_color;
	u32 text_index;
	u32 tree_indent_level;
	Input* input;
	uptr hot_element_id;
	IMGUI_VS_Text_Instance text_buffer[IMGUI_MAX_TEXT_CHARACTERS];

	Max_Length_Array<IMGUI_Element_State, IMGUI_MAX_ELEMENTS> element_states;
};

void imgui_begin(IMGUI_Context* context, Input* input);
void imgui_set_text_cursor(IMGUI_Context* context, v4 color, v2 pos);
void imgui_text(IMGUI_Context* context, char* format_string, ...);
u8   imgui_tree_begin(IMGUI_Context* context, char* name);
void imgui_tree_end(IMGUI_Context* context);
void imgui_f32(IMGUI_Context* context, char* name, f32* val, f32 min_val, f32 max_val);
void imgui_u32(IMGUI_Context* context, char* name, u32* val, u32 min_val, u32 max_val);
u8   imgui_button(IMGUI_Context* context, char* caption);

void render(IMGUI_Context* context, Render* renderer);