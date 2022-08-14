#pragma once

#include "stdafx.h"
#include "prelude.h"
#include "input.h"
#include "jfg_error.h"
#include "types.h"

#include "log.h"

#define CONSOLE_INPUT_BUFFER_LENGTH   512
#define CONSOLE_HISTORY_BUFFER_LENGTH 4096

struct Console
{
	Max_Length_Array<char, CONSOLE_INPUT_BUFFER_LENGTH> input_buffer;
	lua_State *lua_state;
	f32 blink_offset;
	Log log;
	char history_buffer[CONSOLE_HISTORY_BUFFER_LENGTH];
};

void init(Console* console, v2_u32 size, lua_State* lua_state);
bool handle_input(Console* console, Input* input);
void print(Console* console, const char* text);
void render(Console* console, Render* render, f32 time);