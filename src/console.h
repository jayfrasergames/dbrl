#pragma once

#include "stdafx.h"
#include "prelude.h"
#include "input.h"

#define CONSOLE_HISTORY_BUFFER_LENGTH 4096
#define CONSOLE_INPUT_BUFFER_LENGTH   512

struct Console
{
	char history_buffer[CONSOLE_HISTORY_BUFFER_LENGTH];
	u32 history_buffer_pos;
	Max_Length_Array<char, CONSOLE_INPUT_BUFFER_LENGTH> input_buffer;
};

bool init(Console* console, lua_State* lua_state);
bool handle_input(Console* console, Input* input);
void print(Console* console, char* text);