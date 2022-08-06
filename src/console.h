#pragma once

#include "stdafx.h"
#include "prelude.h"
#include "texture.h"
#include "input.h"
#include "render.h"

#define CONSOLE_HISTORY_BUFFER_LENGTH 4096
#define CONSOLE_INPUT_BUFFER_LENGTH   512

struct Console
{
	Texture_ID font_tex_id;
	char history_buffer[CONSOLE_HISTORY_BUFFER_LENGTH];
	u32 history_buffer_pos;
	Max_Length_Array<char, CONSOLE_INPUT_BUFFER_LENGTH> input_buffer;
};

bool init(Console* console, lua_State* lua_state, Render* render, Platform_Functions* platform_functions);
bool handle_input(Console* console, Input* input);
void print(Console* console, char* text);
void render(Console* console, Render* render);