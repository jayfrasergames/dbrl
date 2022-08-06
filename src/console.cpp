#include "console.h"

#include "stdafx.h"

bool init(Console* console, lua_State* lua_state, Render* render, Platform_Functions* platform_functions)
{
	memset(console, 0, sizeof(*console));
	Texture_ID font_tex_id = load_texture(render, "Codepage-437.png", platform_functions);
	if (!font_tex_id) {
		return false;
	}
	return true;
}

bool handle_input(Console* console, Input* input)
{
	if (!input->text_input) {
		return false;
	}

	for (u32 i = 0; i < input->text_input.len; ++i) {
		switch (char c = input->text_input[i]) {
		case '\t':
			// TODO -- tab completion
			break;
		case '\b':
			if (console->input_buffer) {
				--console->input_buffer.len;
			}
			break;
		case '\n':
		case '\r':
			// TODO -- run string in input_buffer
			console->input_buffer.reset();
			break;
		default:
			if (console->input_buffer.len < ARRAY_SIZE(console->input_buffer.items)) {
				console->input_buffer.append(c);
			}
		}
	}

	return true;
}

void print(Console* console, char* text)
{

}