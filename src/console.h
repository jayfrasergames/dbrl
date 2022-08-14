#pragma once

#include "stdafx.h"
#include "prelude.h"
#include "texture.h"
#include "input.h"
#include "render.h"
#include "jfg_error.h"

#define CONSOLE_HISTORY_BUFFER_LENGTH 4096
#define CONSOLE_INPUT_BUFFER_LENGTH   512

enum Console_Scroll
{
	SCROLL_TOP,
	SCROLL_BOTTOM,
	SCROLL_UP_ONE,
	SCROLL_DOWN_ONE,
};

enum Console_Scroll_State
{
	SCROLL_STATE_TOP,
	SCROLL_STATE_BOTTOM,
	SCROLL_STATE_MIDDLE,
};

struct Console
{
	v2_u32 size;
	char history_buffer[CONSOLE_HISTORY_BUFFER_LENGTH];
	u32 start_pos, end_pos, read_pos;
	Max_Length_Array<char, CONSOLE_INPUT_BUFFER_LENGTH> input_buffer;
	Console_Scroll_State scroll_state;
	lua_State *lua_state;
	f32 blink_offset;
};

JFG_Error init(Console* console, v2_u32 size, lua_State* lua_state, Render* render, Platform_Functions* platform_functions);
bool handle_input(Console* console, Input* input);
void print(Console* console, const char* text);
void render(Console* console, Render* render, f32 time);
void scroll(Console* console, Console_Scroll scroll);