#include "console.h"

#include "stdafx.h"

#include "render.h"
#include "log.h"

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

static int l_write_log(lua_State* lua_state)
{
	i32 num_args = lua_gettop(lua_state);
	if (num_args != 1) {
		return luaL_error(lua_state, "Lua function 'write_log_to_file' expected 1 arguments, got %d!", num_args);
	}
	auto console = (Console*)lua_touserdata(lua_state, lua_upvalueindex(1));

	auto filename = luaL_checkstring(lua_state, 1);

	write_to_file(&console->log, filename);

	return 0;
}

void init(Console* console, v2_u32 size, lua_State* lua_state)
{
	memset(console, 0, sizeof(*console));
	init(&console->log, console->history_buffer, ARRAY_SIZE(console->history_buffer));

	console->log.grid_size = v2_u32(size.w, size.h - 1);
	console->lua_state = lua_state;

	lua_pushlightuserdata(lua_state, console);
	lua_pushcclosure(lua_state, l_hello, 1);
	lua_setglobal(lua_state, "hello");

	lua_pushlightuserdata(lua_state, console);
	lua_pushcclosure(lua_state, l_print, 1);
	lua_setglobal(lua_state, "print");

	lua_pushlightuserdata(lua_state, console);
	lua_pushcclosure(lua_state, l_write_log, 1);
	lua_setglobal(lua_state, "write_log_to_file");
}

bool handle_input(Console* console, Input* input)
{
	if (input_get_num_down_transitions(input, INPUT_BUTTON_DOWN)) {
	}
	if (input_get_num_down_transitions(input, INPUT_BUTTON_UP)) {
		char buffer[CONSOLE_INPUT_BUFFER_LENGTH] = {};
		console->input_buffer.append(0);
		strcpy(buffer, console->input_buffer.items);
		strcpy(console->input_buffer.items, console->last_command);
		strcpy(console->last_command, buffer);
		console->input_buffer.len = strlen(console->input_buffer.items);
	}

	if (!input->text_input) {
		return false;
	}

	for (u32 i = 0; i < input->text_input.len; ++i) {
		switch (char c = input->text_input[i]) {
		case '\t':
			// TODO -- tab completion, might be a bit involved for Lua?
			/* 
			// table traversal for table at index t
			// don't know the index for the "global table"
			lua_pushnil(L);  // first key
			while (lua_next(L, t) != 0) {
			// uses 'key' (at index -2) and 'value' (at index -1)
			printf("%s - %s\n",
				lua_typename(L, lua_type(L, -2)),
				lua_typename(L, lua_type(L, -1)));
			// removes 'value'; keeps 'key' for next iteration
			lua_pop(L, 1);
			}
			*/
			break;
		case '\b':
			if (console->input_buffer) {
				--console->input_buffer.len;
			}
			break;
		case '\n':
		case '\r': {
			console->input_buffer.append(0);
			strcpy(console->last_command, console->input_buffer.items);
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
	log(&console->log, text);
}

void render(Console* console, Render* renderer, f32 time)
{
	auto r = &renderer->render_job_buffer;

	v2 glyph_size = v2(9.0f, 16.0f);

	render(&console->log, renderer);

	v2_u32 size = console->log.grid_size + v2_u32(0, 1);

	v2 console_offset = v2(5.0f, 2.0f);
	v2 console_border = v2 (1.0f, 1.0f);
	v2 console_size   = (v2)size;

	v2 top_left = glyph_size * (console_offset + v2(0.0f, console_size.h));
	v2 bottom_right = top_left + glyph_size * (v2(console_size.w, 1.0f) + 2.0f * console_border);
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
	begin_sprites(r, SOURCE_TEXTURE_CODEPAGE437, sprite_constants);

	Sprite_Instance instance = {};
	instance.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
	v2_u32 cursor_pos = v2_u32(0, 0);
	v2 console_top_left = console_offset + console_border;

	cursor_pos = v2_u32(0, size.h);
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