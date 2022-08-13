#include "console.h"

#include "stdafx.h"

const f32 CURSOR_BLINK_DURATION = 0.4f;
const char ESCAPE = 0x1B;

// XXX
static int l_hello(lua_State* lua_state)
{
	i32 num_args = lua_gettop(lua_state);
	if (num_args != 0) {
		return luaL_error(lua_state, "Lua function 'hello' expected 0 arguments, got %d!", num_args);
	}
	auto console = (Console*)lua_touserdata(lua_state, lua_upvalueindex(1));
	print(console, "Hello!");
	return 0;
}

static int l_print(lua_State* lua_state)
{
	u32 num_args = lua_gettop(lua_state);
	auto console = (Console*)lua_touserdata(lua_state, lua_upvalueindex(1));
	if (num_args == 0) {
		print(console, "");
	}
	for (u32 i = 0; i < num_args; ++i) {
		u32 arg = i + 1;
		int type = lua_type(lua_state, arg);
		char prefix_buffer[16] = {};
		if (num_args > 1) {
			// print(console, fmt("%u:", arg));
			snprintf(prefix_buffer, ARRAY_SIZE(prefix_buffer), "%u: ", arg);
		}
		switch (type) {
		case LUA_TNIL:
			print(console, fmt("%snil", prefix_buffer));
			break;
		case LUA_TNUMBER: {
			auto number = lua_tonumber(lua_state, arg);
			print(console, fmt("%s%f", prefix_buffer, number));
			break;
		}
		case LUA_TBOOLEAN: {
			const char *str = lua_toboolean(lua_state, arg) ? "true" : "false";
			print(console, fmt("%s%s", prefix_buffer, str));
			break;
		}
		case LUA_TSTRING:
			print(console, fmt("%s%s", prefix_buffer, lua_tostring(lua_state, arg)));
			break;
		case LUA_TTABLE:
			print(console, fmt("%stable object (TODO -- print these)", prefix_buffer));
			break;
		case LUA_TFUNCTION:
			print(console, fmt("%sfunction object (TODO -- print these)", prefix_buffer));
			break;
		case LUA_TUSERDATA:
			print(console, fmt("%suserdata at <%p>", prefix_buffer, lua_topointer(lua_state, arg)));
			break;
		case LUA_TTHREAD:
			print(console, fmt("%sthread object (TODO -- print these)", prefix_buffer));
			break;
		case LUA_TLIGHTUSERDATA:
			print(console, fmt("%slightuserdata at <%p>", prefix_buffer, lua_topointer(lua_state, arg)));
			break;
		}
	}
	return 0;
}

bool init(Console* console, v2_u32 size, lua_State* lua_state, Render* render, Platform_Functions* platform_functions)
{
	memset(console, 0, sizeof(*console));
	console->font_tex_id = get_texture_id(render, "Codepage-437.png");
	if (console->font_tex_id == INVALID_TEXTURE_ID) {
		return false;
	}
	console->size = size;
	console->scroll_state = SCROLL_STATE_BOTTOM;
	console->lua_state = lua_state;

	lua_pushlightuserdata(lua_state, console);
	lua_pushcclosure(lua_state, l_hello, 1);
	lua_setglobal(lua_state, "hello");

	lua_pushlightuserdata(lua_state, console);
	lua_pushcclosure(lua_state, l_print, 1);
	lua_setglobal(lua_state, "print");
	// lua_
	// lua_register(lua_state, "hello", lua_hello);


	return true;
}

bool handle_input(Console* console, Input* input)
{
	if (input_get_num_down_transitions(input, INPUT_BUTTON_DOWN)) {
		scroll(console, SCROLL_DOWN_ONE);
	}
	if (input_get_num_down_transitions(input, INPUT_BUTTON_UP)) {
		scroll(console, SCROLL_UP_ONE);
	}

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
		case '\r': {
			console->input_buffer.append(0);
			print(console, fmt("> %s", console->input_buffer.items));
			int error = luaL_dostring(console->lua_state, console->input_buffer.items);
			if (error != LUA_OK) {
				print(console, fmt("Failed to run Lua command: \"%s\".", console->input_buffer.items));
				print(console, (char*)lua_tostring(console->lua_state, -1));
			}
			console->input_buffer.reset();

			break;
		}
		default:
			if (console->input_buffer.len < ARRAY_SIZE(console->input_buffer.items) - 1) {
				console->input_buffer.append(c);
				console->blink_offset = 0.0f;
			}
		}
	}

	return true;
}

void print(Console* console, const char* text)
{
	u32 buffer_pos = console->end_pos;
	for (const char *p = text; *p; ++p) {
		console->history_buffer[buffer_pos % ARRAY_SIZE(console->history_buffer)] = *p;
		++buffer_pos;
	}
	console->history_buffer[buffer_pos % ARRAY_SIZE(console->history_buffer)] = '\n';
	console->end_pos = ++buffer_pos;

	if (console->history_buffer[buffer_pos % ARRAY_SIZE(console->history_buffer)]) {
		while (console->history_buffer[buffer_pos % ARRAY_SIZE(console->history_buffer)] != '\n') {
			console->history_buffer[buffer_pos % ARRAY_SIZE(console->history_buffer)] = 0;
			++buffer_pos;
		}
		console->history_buffer[buffer_pos % ARRAY_SIZE(console->history_buffer)] = 0;
		console->start_pos = ++buffer_pos;
	}
	if (console->scroll_state == SCROLL_STATE_BOTTOM) {
		scroll(console, SCROLL_BOTTOM);
	}
}

void render(Console* console, Render* render, f32 time)
{
	auto r = &render->render_job_buffer;

	v2 glyph_size = v2(9.0f, 16.0f);

	v2_u32 size = console->size;

	v2 console_offset = v2(5.0f, 2.0f);
	v2 console_border = v2 (1.0f, 1.0f);
	v2 console_size   = (v2)size;

	v2 top_left = glyph_size * console_offset;
	v2 bottom_right = top_left + glyph_size * (console_size + 2.0f * console_border);
	v2 top_right = v2(bottom_right.x, top_left.y);
	v2 bottom_left = v2(top_left.x, bottom_right.y);

	Triangle_Instance t = {};
	t.color = v4(0.0f, 0.0f, 0.0f, 0.5f);
	t.a = top_left;
	t.b = top_right;
	t.c = bottom_left;
	push_triangle(r, t);
	t.a = bottom_right;
	push_triangle(r, t);

	Sprite_Constants sprite_constants = {};
	sprite_constants.tile_input_size = glyph_size;
	sprite_constants.tile_output_size = glyph_size;
	begin_sprites(r, console->font_tex_id, sprite_constants);

	Sprite_Instance instance = {};
	instance.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
	v2_u32 cursor_pos = v2_u32(0, 0);
	v2 console_top_left = console_offset + console_border;

	for (u32 pos = console->read_pos % ARRAY_SIZE(console->history_buffer); console->history_buffer[pos]; pos = (pos + 1) % ARRAY_SIZE(console->history_buffer)) {
		u8 c = console->history_buffer[pos];
		switch (c) {
		case '\n':
		case '\r':
			++cursor_pos.y;
			cursor_pos.x = 0;
			break;
		case '\t':
			cursor_pos.x += 8;
			cursor_pos.x &= ~7;
			break;
		default:
			instance.glyph_coords = v2(c % 32, c / 32);
			instance.output_coords = (v2)cursor_pos + console_top_left;
			push_sprite(r, instance);
			++cursor_pos.x;
			break;
		}
		if (cursor_pos.x >= size.w) {
			++cursor_pos.y;
			cursor_pos.x = 0;
		}
		if (cursor_pos.y >= size.h - 1) {
			break;
		}
	}
end_history_buffer_print:

	cursor_pos = v2_u32(0, size.h - 1);
	u8 prompt = '>';
	instance.glyph_coords = v2(prompt % 32, prompt / 32);
	instance.output_coords = (v2)cursor_pos + console_top_left;
	push_sprite(r, instance);
	cursor_pos.x += 2;
	u32 start_pos = console->input_buffer.len > size.w - 3 ? console->input_buffer.len + 3 - size.w : 0;
	for (u32 i = start_pos; i < console->input_buffer.len; ++i) {
		u8 c = console->input_buffer[i];
		instance.glyph_coords = v2(c % 32, c / 32);
		instance.output_coords = (v2)cursor_pos + console_top_left;
		push_sprite(r, instance);
		++cursor_pos.x;
	}

	if (console->blink_offset == 0.0f) {
		console->blink_offset = time;
	}
	time -= console->blink_offset;
	if ((u32)floorf(time / CURSOR_BLINK_DURATION) % 2 == 0) {
		instance.glyph_coords = v2(27.0f, 6.0f);
		instance.output_coords = (v2)cursor_pos + console_top_left;
		push_sprite(r, instance);
	}
}

static u32 get_next_line_start(Console* console, u32 pos)
{
	u32 width = console->size.w;
	u32 end_pos = console->end_pos;
	if (pos == end_pos) {
		return pos;
	}
	u32 col_pos = 0;
	for ( ; pos < end_pos; ++pos) {
		u8 c = console->history_buffer[pos];
		switch (c) {
		case '\n':
		case '\r':
			return pos + 1;
		case '\t':
			if (col_pos >= width) {
				return pos;
			}
			col_pos += 8;
			col_pos &= ~7;
			break;
		default:
			if (col_pos >= width) {
				return pos;
			}
			++col_pos;
			break;
		}
	}
	return pos;
}

static u32 get_prev_line_start(Console* console, u32 pos)
{
	u32 start_pos = console->start_pos;
	if (pos == start_pos) {
		return start_pos;
	}
	u32 prev_newline_pos = pos - 1;
	while (1) {
		char c = console->history_buffer[(prev_newline_pos - 1) % ARRAY_SIZE(console->history_buffer)];
		if (c == '\n' || c == 0) {
			break;
		}
		--prev_newline_pos;
	}
	u32 result = prev_newline_pos;
	u32 cur_pos = result;
	while (cur_pos < pos) {
		result = cur_pos;
		cur_pos = get_next_line_start(console, cur_pos);
	}
	return result;
}

void scroll(Console* console, Console_Scroll scroll)
{
	switch (scroll) {
	case SCROLL_TOP:
		console->read_pos = console->start_pos;
		console->scroll_state = SCROLL_STATE_TOP;
		break;
	case SCROLL_UP_ONE:
		console->read_pos = get_prev_line_start(console, console->read_pos);
		console->scroll_state = SCROLL_STATE_MIDDLE;
		break;
	case SCROLL_DOWN_ONE:
		console->read_pos = get_next_line_start(console, console->read_pos);
		console->scroll_state = SCROLL_STATE_MIDDLE;
		break;
	case SCROLL_BOTTOM: {
		u32 pos = console->end_pos;
		for (u32 i = 0; i < console->size.h - 1; ++i) {
			pos = get_prev_line_start(console, pos);
		}
		console->read_pos = pos;
		console->scroll_state = SCROLL_STATE_BOTTOM;
		break;
	}
	}
}