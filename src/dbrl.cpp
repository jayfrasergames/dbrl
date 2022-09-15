#include "dbrl.h"

#include "stdafx.h"

#include "imgui.h"
#include "jfg_math.h"
#include "log.h"
#include "debug_line_draw.h"
#include "containers.hpp"
#include "random.h"
#include "thread.h"

#include "types.h"

#include "fov.h"
#include "assets.h"
#include "constants.h"
#include "debug_draw_world.h"
#include "sound.h"
#include "sprite_sheet.h"
#include "card_render.h"
#include "particles.h"
#include "pathfinding.h"
// #include "physics.h"
#include "console.h"
#include "appearance.h"

#include "gen/cards.data.h"
#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"
#include "gen/sprite_sheet_water_edges.data.h"
#include "gen/sprite_sheet_effects_24.data.h"
#include "gen/sprite_sheet_effects_32.data.h"
#include "gen/boxy_bold.data.h"

#include "draw.h"
#include "texture.h"
#include "render.h"

#include "game.h"
#include "level_gen.h"
#include "ui.h"

// =============================================================================
// type definitions/constants

#define CARD_STATE_MAX_EVENTS 1024

struct Program;
thread_local Program *global_program;

// =============================================================================
// game

#define GAME_MAX_ACTIONS 1024
typedef Max_Length_Array<Action, GAME_MAX_ACTIONS> Action_Buffer;

// Transactions
#define MAX_EVENTS 10240

// maybe better called "world state"?
Entity_ID game_new_entity_id(Game *game)
{
	return game->next_entity_id++;
}

Entity_ID game_get_player_id(Game* game)
{
	auto& controllers = game->controllers;
	Entity_ID player_id = 0;
	for (u32 i = 0; i < controllers.len; ++i) {
		if (controllers[i].type == CONTROLLER_PLAYER) {
			player_id = controllers[i].player.entity_id;
			break;
		}
	}
	ASSERT(player_id);
	return player_id;
}

Pos game_get_player_pos(Game* game)
{
	Entity_ID player_id = game_get_player_id(game);
	for (u32 i = 0; i < game->entities.len; ++i) {
		Entity *e = &game->entities[i];
		if (e->id == player_id) {
			return e->pos;
		}
	}
	ASSERT(0);
	return Pos(0, 0);
}

u8 calculate_line_of_sight(Game *game, Entity_ID vision_entity_id, Pos target)
{
	Entity *e = get_entity_by_id(game, vision_entity_id);
	v2_i32 start = (v2_i32)e->pos;
	v2_i32 end = (v2_i32)target;

	v2_i32 dir = end - start;
	if (!dir) {
		return 1;
	}

	if (!dir.x || !dir.y || (abs(dir.x) == abs(dir.y))) {
		// vertical, horizontal or diagonal line
		v2_i32 pos = start;
		if (dir.x) { dir.x /= abs(dir.x); }
		if (dir.y) { dir.y /= abs(dir.y); }
		pos += dir;
		while (pos != end) {
			Pos p = (v2_u8)pos;
			if (game_is_pos_opaque(game, p)) {
				return 0;
			}
			pos += dir;
		}
		return 1;
	}

	struct Sector
	{
		Rational start;
		Rational end;
	};

	const i32 half_cell_size = 2;
	const i32 cell_margin = 1;
	const i32 cell_size = 2 * half_cell_size;
	const i32 cell_inner_size = cell_size - 2 * cell_margin;
	const Rational wall_see_low  = Rational::cancel(1, 6);
	const Rational wall_see_high = Rational::cancel(5, 6);

	Pos vision_pos = e->pos;

	u8 octant_id = 0;
	v2_i32 transformed_dir = dir;
	if (dir.y > 0) {
		octant_id += 2;
	} else {
		transformed_dir.y *= -1;
	}
	if (dir.x < 0) {
		octant_id += 4;
		transformed_dir.x *= -1;
	}
	if (abs(dir.x) > abs(dir.y)) {
		octant_id += 1;
		i32 tmp = transformed_dir.x;
		transformed_dir.x = transformed_dir.y;
		transformed_dir.y = tmp;
	}

	/*
	octants:

	\ 4  | 0  /
	5 \  |  / 1
	    \|/
	-----+-----
	    /|\
	7 /  |  \ 3
	/ 6  |  2 \
	*/

	Max_Length_Array<Sector, 16> sectors_1, sectors_2;
	Max_Length_Array<Sector, 16> *sectors_front = &sectors_1, *sectors_back = &sectors_2;

	sectors_front->reset();
	sectors_back->reset();
	{
		Sector s = {};
		s.start = Rational::cancel(transformed_dir.x * cell_size - half_cell_size,
		                           transformed_dir.y * cell_size + half_cell_size);
		s.end = Rational::cancel(transformed_dir.x * cell_size + half_cell_size,
		                         transformed_dir.y * cell_size - half_cell_size);
		sectors_front->append(s);
	}

	v2_i32 up, left;
	switch (octant_id) {
	case 0: up = v2_i32( 0, -1); left = v2_i32(-1,  0); break;
	case 1: up = v2_i32( 1,  0); left = v2_i32( 0,  1); break;
	case 2: up = v2_i32( 0,  1); left = v2_i32(-1,  0); break;
	case 3: up = v2_i32( 1,  0); left = v2_i32( 0, -1); break;
	case 4: up = v2_i32( 0, -1); left = v2_i32( 1,  0); break;
	case 5: up = v2_i32(-1,  0); left = v2_i32( 0,  1); break;
	case 6: up = v2_i32( 0,  1); left = v2_i32( 1,  0); break;
	case 7: up = v2_i32(-1,  0); left = v2_i32( 0, -1); break;
	}

	for (u16 y_iter = 0; y_iter <= transformed_dir.y; ++y_iter) {
		auto& old_sectors = *sectors_front;
		auto& new_sectors = *sectors_back;
		new_sectors.reset();

		if (!old_sectors) {
			break;
		}

		for (u32 i = 0; i < old_sectors.len; ++i) {
			Sector s = old_sectors[i];

			u16 x_start = ((y_iter * cell_size - cell_size / 2) * s.start.numerator
					+ (s.start.denominator * cell_size / 2))
				      / (s.start.denominator * cell_size);

			u16 x_end = ((y_iter * cell_size + cell_size / 2) * s.end.numerator
				      + (s.end.denominator * cell_size / 2) - 1)
				    / (cell_size * s.end.denominator);

			u8 prev_was_clear = 0;
			for (u16 x_iter = x_start; x_iter <= x_end; ++x_iter) {
				v2_i32 logical_pos = v2_i32(x_iter, y_iter);
				v2_i32 cur_pos;
				switch (octant_id) {
				case 0:
					cur_pos.x = (i32)vision_pos.x + (i32)x_iter;
					cur_pos.y = (i32)vision_pos.y - (i32)y_iter;
					break;
				case 1:
					cur_pos.x = (i32)vision_pos.x + (i32)y_iter;
					cur_pos.y = (i32)vision_pos.y - (i32)x_iter;
					break;
				case 2:
					cur_pos.x = (i32)vision_pos.x + (i32)x_iter;
					cur_pos.y = (i32)vision_pos.y + (i32)y_iter;
					break;
				case 3:
					cur_pos.x = (i32)vision_pos.x + (i32)y_iter;
					cur_pos.y = (i32)vision_pos.y + (i32)x_iter;
					break;
				case 4:
					cur_pos.x = (i32)vision_pos.x - (i32)x_iter;
					cur_pos.y = (i32)vision_pos.y - (i32)y_iter;
					break;
				case 5:
					cur_pos.x = (i32)vision_pos.x - (i32)y_iter;
					cur_pos.y = (i32)vision_pos.y - (i32)x_iter;
					break;
				case 6:
					cur_pos.x = (i32)vision_pos.x - (i32)x_iter;
					cur_pos.y = (i32)vision_pos.y + (i32)y_iter;
					break;
				case 7:
					cur_pos.x = (i32)vision_pos.x - (i32)y_iter;
					cur_pos.y = (i32)vision_pos.y + (i32)x_iter;
					break;
				}

				if (!(0 < cur_pos.y && cur_pos.y < 255)) { goto break_outer_loop; }
				if (!(0 < cur_pos.x && cur_pos.x < 255)) { x_end = x_iter; }
				if (game_is_pos_opaque(game, (v2_u8)cur_pos)) {
					bool top    = game_is_pos_opaque(game, (v2_u8)(cur_pos + up));
					bool bottom = game_is_pos_opaque(game, (v2_u8)(cur_pos - up));
					bool left_  = game_is_pos_opaque(game, (v2_u8)(cur_pos + left));
					bool right  = game_is_pos_opaque(game, (v2_u8)(cur_pos - left));
					bool btl = !top    && !left_;
					bool bbr = !bottom && !right;
					u8 horiz_visible = 0;
					Rational horiz_left_intersect = Rational::cancel(
						s.start.numerator * ((i32)y_iter * cell_size
								      - half_cell_size)
						+ s.start.denominator * (half_cell_size
								     - (i32)x_iter * cell_size),
						s.start.denominator * cell_size
					);
					Rational horiz_right_intersect = Rational::cancel(
						s.end.numerator * ((i32)y_iter * cell_size
								      - half_cell_size)
						+ s.end.denominator * (half_cell_size
								       - (i32)x_iter * cell_size),
						s.end.denominator * cell_size
					);
					Rational vert_left_intersect = Rational::cancel(
						s.start.denominator * (2 * x_iter - 1)
						+ s.start.numerator * (1 - 2 * y_iter),
						2 * s.start.numerator
					);
					Rational vert_right_intersect = Rational::cancel(
						s.end.denominator * (2 * x_iter - 1)
						+ s.end.numerator * (1 - 2 * y_iter),
						2 * s.end.numerator
					);
					u8 vert_visible = 0;

					if (logical_pos == transformed_dir) {
						if (horiz_left_intersect  <= wall_see_high
						 && horiz_right_intersect >= wall_see_low) {
							return 1;
						} else if (prev_was_clear
							&& vert_left_intersect  >= wall_see_low
							&& vert_right_intersect <= wall_see_high) {
							return 1;
						}
					}

					Rational left_slope, right_slope;
					if (btl) {
						left_slope = Rational::cancel(
							(i32)x_iter * cell_size - cell_size / 2,
							(i32)y_iter * cell_size
						);
					} else {
						left_slope = Rational::cancel(
							(i32)x_iter * cell_size - cell_size / 2,
							(i32)y_iter * cell_size + cell_size / 2
						);
					}
					if (bbr) {
						right_slope = Rational::cancel(
							(i32)x_iter * cell_size + cell_size / 2,
							(i32)y_iter * cell_size
						);
					} else {
						right_slope = Rational::cancel(
							(i32)x_iter * cell_size + cell_size / 2,
							(i32)y_iter * cell_size - cell_size / 2
						);
					}

					if (prev_was_clear) {
						Sector new_sector = s;
						if (left_slope < new_sector.end) {
							new_sector.end = left_slope;
						}
						if (new_sector.end > new_sector.start) {
							new_sectors.append(new_sector);
						}
						prev_was_clear = 0;
					}
					s.start = right_slope;
				} else {
					// Sector s = old_sectors[cur_sector];
					Rational left_slope = Rational::cancel(
						(i32)x_iter * cell_size + cell_margin - cell_size / 2,
						(i32)y_iter * cell_size - cell_margin + cell_size / 2
					);
					Rational right_slope = Rational::cancel(
						(i32)x_iter * cell_size - cell_margin + cell_size / 2,
						(i32)y_iter * cell_size + cell_margin - cell_size / 2
					);
					if (right_slope > s.start && left_slope < s.end) {
						if (logical_pos == transformed_dir) {
							return 1;
						}
					}
					prev_was_clear = 1;
				}
			}
			if (prev_was_clear) {
				if (s.end > s.start) {
					new_sectors.append(s);
				}
			}
		}

		// swap sector buffers
		{
			auto tmp = sectors_front;
			sectors_front = sectors_back;
			sectors_back = tmp;
		}
	}

break_outer_loop:

	return 0;
}

// =============================================================================
// program

enum Program_State
{
	PROGRAM_STATE_NORMAL,
	PROGRAM_STATE_DEBUG_PAUSE,
	PROGRAM_STATE_NO_PAUSE,
};

#define PROGRAM_INPUT_STATES \
	PROGRAM_INPUT_STATE(NONE) \
	PROGRAM_INPUT_STATE(PLAYING_CARDS) \
	PROGRAM_INPUT_STATE(DRAGGING_MAP) \
	PROGRAM_INPUT_STATE(CARD_PARAMS) \
	PROGRAM_INPUT_STATE(ANIMATING)

#define MAX_PROGRAM_INPUT_STATES 32
enum Program_Input_State
{
#define PROGRAM_INPUT_STATE(name) GIS_##name,
	PROGRAM_INPUT_STATES
#undef PROGRAM_INPUT_STATE
};

char *PROGRAM_INPUT_STATE_NAMES[] = {
#define PROGRAM_INPUT_STATE(name) #name,
	PROGRAM_INPUT_STATES
#undef PROGRAM_INPUT_STATE
};

struct Program
{
	lua_State *lua_state;

	bool is_console_visible;
	bool display_fog_of_war;

	Console console;
	Platform_Functions platform_functions;

	Program_State       state;
	volatile u32        process_frame_signal;
	volatile u32        debug_resume;

	Log                 debug_log;
	Log                 debug_pause_log;

	Input  *cur_input;
	v2_u32  cur_screen_size;

	u8                                 display_debug_ui;
	Game                               game;

	Stack<Program_Input_State, MAX_PROGRAM_INPUT_STATES> program_input_state_stack;
	u32                                           cur_card_param;
	Max_Length_Array<Card_Param, MAX_CARD_PARAMS> card_params;
	Action                             action_being_built;

	Anim_State                         anim_state;
	Render                            *render;
	Draw                              *draw;
	Sound_Player                       sound;

	u32           frame_number;
	v2            screen_size;
	v2_u32        max_screen_size;

	MT19937 random_state;

	Assets_Header assets_header;
	u8 assets_data[ASSETS_DATA_MAX_SIZE];
};

Memory_Spec get_program_size()
{
	Memory_Spec result = {};
	result.alignment = alignof(Program);
	result.size = sizeof(Program);
	return result;
}

void set_global_state(Program* program)
{
	global_program = program;
	// debug_log = &program->debug_log;
	// debug_pause_log = &program->debug_pause_log;

	program->random_state.set_current();
	program->draw->debug_draw_world.set_current();
}

void build_deck_random_n(Program *program, u32 n)
{
	// cards
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->anim_state.card_anim_state;
	// memset(card_state, 0, sizeof(*card_state));
	memset(card_anim_state, 0, sizeof(*card_anim_state));

	for (u32 i = 0; i < n; ++i) {
		auto appearance = (Card_Appearance)(rand_u32() % NUM_CARD_APPEARANCES);
		add_card(&program->game, appearance);
	}
}

void build_lightning_deck(Program *program)
{
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->anim_state.card_anim_state;
	memset(card_state, 0, sizeof(*card_state));
	memset(card_anim_state, 0, sizeof(*card_anim_state));

	for (u32 i = 0; i < 50; ++i) {
		Card card = {};
		card.id = i + 1;
		card.appearance = CARD_APPEARANCE_LIGHTNING;
		card_state->discard.append(card);
	}
}

void build_deck_poison(Program *program)
{
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->anim_state.card_anim_state;
	memset(card_state, 0, sizeof(*card_state));
	memset(card_anim_state, 0, sizeof(*card_anim_state));

	for (u32 i = 0; i < 10; ++i) {
		Card card = {};
		card.id = i + 1;
		card.appearance = CARD_APPEARANCE_POISON;
		card_state->discard.append(card);
	}
}

void load_assets(void* uncast_program)
{
	Program *program = (Program*)uncast_program;
	u8 loaded_assets = try_load_assets(&program->platform_functions, &program->assets_header);
	ASSERT(loaded_assets);

	sound_player_load_sounds(&program->sound, &program->assets_header);
}

void program_init_level(Program* program, Build_Level_Function build, Log* log)
{
	build(&program->game, log);
	build_deck_random_n(program, 100);

	program->anim_state.camera.zoom = 14.0f;
	Pos player_pos = game_get_player_pos(&program->game);
	program->anim_state.camera.world_center = (v2)player_pos;

	memset(&program->anim_state, 0, sizeof(program->anim_state));
	program->anim_state.draw = program->draw;
	init(&program->anim_state, &program->game);
}

static int l_build_level(lua_State* lua_state)
{
	i32 num_args = lua_gettop(lua_state);
	if (num_args != 0) {
		return luaL_error(lua_state, "Lua function 'build_level' expected 0 arguments, got %d!", num_args);
	}
	auto program = (Program*)lua_touserdata(lua_state, lua_upvalueindex(1));
	auto func = (Build_Level_Function)lua_touserdata(lua_state, lua_upvalueindex(2));

	program_init_level(program, func, &program->console.log);

	return 0;
}

static int l_zoom_whole_map(lua_State* lua_state)
{
	i32 num_args = lua_gettop(lua_state);
	if (num_args != 0) {
		return luaL_error(lua_state, "Lua function 'zoom_whole_map' expected 0 arguments, got %d!", num_args);
	}
	auto program = (Program*)lua_touserdata(lua_state, lua_upvalueindex(1));

	v2_u32 size = {};
	v2_u32 top_left = v2_u32(256, 256);
	v2_u32 bottom_right = v2_u32(0, 0);

	for (u32 y = 0; y < 256; ++y) {
		for (u32 x = 0; x < 256; ++x) {
			v2_u32 p = v2_u32(x, y);
			if (program->game.tiles[(Pos)p].type != TILE_EMPTY) {
				top_left = jfg_min(top_left, p);
				bottom_right = jfg_max(bottom_right, p);
			}
		}
	}

	size = bottom_right - top_left + v2_u32(1, 1);
	program->anim_state.camera.world_center = (v2)top_left + (v2)size / 2.0f;
	program->anim_state.camera.zoom = (f32)size.h;

	return 0;
}

static int l_set_fov(lua_State* lua_state)
{
	i32 num_args = lua_gettop(lua_state);
	if (num_args != 1) {
		return luaL_error(lua_state, "Lua function 'set_fov' expected 1 arguments, got %d!", num_args);
	}
	if (!lua_isboolean(lua_state, 1)) {
		return luaL_error(lua_state, "Lua function \"set_fov\" expects a boolean argument!");
	}
	auto fov_enabled = lua_toboolean(lua_state, 1);

	auto program = (Program*)lua_touserdata(lua_state, lua_upvalueindex(1));
	program->display_fog_of_war = fov_enabled;

	return 0;
}

static int l_random_cards(lua_State* lua_state)
{

	i32 num_args = lua_gettop(lua_state);
	if (num_args != 1) {
		return luaL_error(lua_state, "Lua function 'random_cards' expected 1 arguments, got %d!", num_args);
	}

	u32 num_cards = luaL_checkinteger(lua_state, 1);
	auto program = (Program*)lua_touserdata(lua_state, lua_upvalueindex(1));

	build_deck_random_n(program, num_cards);

	return 0;
}

void program_init(Program* program, Draw* draw, Render* render, Platform_Functions platform_functions)
{
	memset(program, 0, sizeof(*program));

#define PLATFORM_FUNCTION(_return_type, name, ...) program->platform_functions.name = platform_functions.name;
	PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION

	program->render = render;
	program->draw = draw;
	program->anim_state.draw = draw;
	program->display_fog_of_war = true;

	lua_State *lua_state = luaL_newstate();
	program->lua_state = lua_state;

	init(&program->console, v2_u32(80, 25), program->lua_state);
	register_lua_functions(render, &program->console.log, program->lua_state);

	// register program lua functions
	{
		lua_pushlightuserdata(lua_state, program);
		lua_pushcclosure(lua_state, l_set_fov, 1);
		lua_setglobal(lua_state, "set_fov");

		lua_pushlightuserdata(lua_state, program);
		lua_pushcclosure(lua_state, l_zoom_whole_map, 1);
		lua_setglobal(lua_state, "zoom_whole_map");

		lua_pushlightuserdata(lua_state, program);
		lua_pushcclosure(lua_state, l_random_cards, 1);
		lua_setglobal(lua_state, "random_cards");

		for (u32 i = 0; i < NUM_LEVEL_GEN_FUNCS; ++i) {
			auto func_data = &BUILD_LEVEL_FUNCS[i];
			lua_pushlightuserdata(lua_state, program);
			lua_pushlightuserdata(lua_state, func_data->func);
			lua_pushcclosure(lua_state, l_build_level, 2);
			lua_setglobal(lua_state, func_data->name);
		}
	}

	set_global_state(program);
	program->state = PROGRAM_STATE_NO_PAUSE;
	program->random_state.seed(0);

	sprite_sheet_renderer_init(&program->draw->renderer,
	                           &program->draw->tiles, 4,
	                           { 1600, 900 });


	program->draw->tiles.data         = SPRITE_SHEET_TILES;
	program->draw->creatures.data     = SPRITE_SHEET_CREATURES;
	program->draw->water_edges.data   = SPRITE_SHEET_WATER_EDGES;
	program->draw->effects_24.data    = SPRITE_SHEET_EFFECTS_24;
	program->draw->effects_32.data    = SPRITE_SHEET_EFFECTS_32;
	program->draw->boxy_bold.tex_size = boxy_bold_texture_size;
	program->draw->boxy_bold.tex_data = boxy_bold_pixel_data;

	// build_level_default(program);
	program_init_level(program, build_level_spider_room, NULL);

	program->sound.set_ambience(SOUND_CARD_GAME_AMBIENCE_CAVE);

	program->state = PROGRAM_STATE_NORMAL;
	program->program_input_state_stack.push(GIS_NONE);
}

u8 program_dsound_init(Program* program, IDirectSound* dsound)
{
	if (!sound_player_dsound_init(&program->sound, &program->assets_header, dsound)) {
		return 0;
	}
	// load_assets(program);
	start_thread(load_assets, "load_assets", program);
	return 1;
}

void program_dsound_free(Program* program)
{
	sound_player_dsound_free(&program->sound);
}

void program_dsound_play(Program* program)
{
	sound_player_dsound_play(&program->sound);
}

static v2 screen_pos_to_world_pos(Camera* camera, v2_u32 screen_size, v2_u32 screen_pos)
{
	v2 p = (v2)screen_pos - ((v2)screen_size / 2.0f);
	f32 raw_zoom = (f32)screen_size.y / (camera->zoom * 24.0f);
	v2 world_pos = p / raw_zoom + 24.0f * (camera->world_center + camera->offset);
	return world_pos;
}

void debug_pause()
{
	Program *program = global_program;
	if (program->state == PROGRAM_STATE_NO_PAUSE) {
		return;
	}
	program->state = PROGRAM_STATE_DEBUG_PAUSE;
	interlocked_compare_exchange(&program->process_frame_signal, 1, 0);
	while (!interlocked_compare_exchange(&program->debug_resume, 0, 1)) {
		sleep(0);
	}
}

void process_frame_aux(Program* program, Input* input, v2_u32 screen_size)
{
	// start off by setting global state
	set_global_state(program);
	// log_reset(debug_log);

	// render
	// XXX -- put this here for now - later we'll gather rendering into one
	// place
	reset(&program->render->render_job_buffer);

	program->draw->debug_draw_world.reset();

	program->screen_size = (v2)screen_size;
	++program->frame_number;
	program->sound.reset();

	// XXX -- put this somewhere sensible
	if (program->is_console_visible) {
		handle_input(&program->console, input);
	}

	// process input
	switch (program->program_input_state_stack.peek()) {
	case GIS_ANIMATING:
	case GIS_CARD_PARAMS:
	case GIS_PLAYING_CARDS:
	case GIS_NONE:
		if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_ENDED_DOWN) {
			program->program_input_state_stack.push(GIS_DRAGGING_MAP);
		}
		break;
	case GIS_DRAGGING_MAP:
		if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_HELD_DOWN) {
			f32 raw_zoom = (f32)screen_size.y / program->anim_state.camera.zoom;
			program->anim_state.camera.world_center -= (v2)input->mouse_delta / raw_zoom;
		} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
		             & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
			program->program_input_state_stack.pop();
		}
		break;
	}
	program->anim_state.camera.zoom -= (f32)input->mouse_wheel_delta * 0.25f;

	if (input_get_num_up_transitions(input, INPUT_BUTTON_F1) % 2) {
		program->display_debug_ui = !program->display_debug_ui;
	}
	if (input_get_num_up_transitions(input, INPUT_BUTTON_F2) % 2) {
		program->is_console_visible = !program->is_console_visible;
	}

	f32 time = (f32)program->frame_number / 60.0f;
	program->draw->renderer.time = time;

	if (program->program_input_state_stack.peek() == GIS_ANIMATING && !is_animating(&program->anim_state)) {
		program->program_input_state_stack.pop();
	}

	auto ui_mouse_over = get_mouse_over(&program->anim_state, input->mouse_pos, screen_size);

	// XXX -- need to be building up an action
	// due to card params
	Action player_action = {};
	player_action.type = ACTION_NONE;
	player_action.entity_id = get_player(&program->game)->id;

	switch (program->program_input_state_stack.peek()) {
	case GIS_NONE:
		if (input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			switch (ui_mouse_over.type) {
			case UI_MOUSE_OVER_NOTHING:
				break;
			case UI_MOUSE_OVER_ENTITY: {
				auto entity = get_entity_by_id(&program->game, ui_mouse_over.entity.entity_id);
				Action_Type action_type = ACTION_MOVE;
				if (entity && entity->default_action) {
					action_type = entity->default_action;
				}
				switch (action_type) {
				case ACTION_MOVE: {
					auto player = get_player(&program->game);
					Pos end = ui_mouse_over.world_pos;
					if (entity) {
						end = entity->pos;
					}
					Pos start = player->pos;
					if (positions_are_adjacent(start, end) && is_pos_passable(&program->game, end, player->movement_type)) {
						player_action.type = ACTION_MOVE;
						player_action.move.start = start;
						player_action.move.end = end;
					}
					break;
				}
				case ACTION_OPEN_DOOR: {
					auto player = get_player(&program->game);
					if (positions_are_adjacent(player->pos, entity->pos)) {
						player_action.type = action_type;
						player_action.open_door.door_id = entity->id;
					}
					break;
				}
				case ACTION_CLOSE_DOOR: {
					auto player = get_player(&program->game);
					if (positions_are_adjacent(player->pos, entity->pos)) {
						player_action.type = action_type;
						player_action.close_door.door_id = entity->id;
					}
					break;
				}
				case ACTION_BUMP_ATTACK: {
					auto player = get_player(&program->game);
					if (positions_are_adjacent(player->pos, entity->pos)) {
						player_action.type = action_type;
						player_action.bump_attack.target_id = entity->id;
					}
					break;
				}
				default:
					ASSERT(0);
					break;
				}
				break;
			}
			case UI_MOUSE_OVER_DISCARD:
			case UI_MOUSE_OVER_DECK:
				player_action.type = ACTION_DRAW_CARDS;
				program->program_input_state_stack.push(GIS_PLAYING_CARDS);
				break;
			case UI_MOUSE_OVER_CARD:
				break;
			default:
				ASSERT(0);
				break;
			}
		}
		break;

	case GIS_PLAYING_CARDS: {
		if (ui_mouse_over.type == UI_MOUSE_OVER_CARD) {
			highlight_card(&program->anim_state.card_anim_state, ui_mouse_over.card.card_id);
		} else {
			highlight_card(&program->anim_state.card_anim_state, 0);
		}
		if (input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			switch (ui_mouse_over.type) {
			case UI_MOUSE_OVER_CARD: {
				program->cur_card_param = 0;
				Action_Type action_type = ACTION_NONE;
				get_card_params(&program->game, ui_mouse_over.card.card_id, &action_type, program->card_params);
				if (program->card_params) {
					program->program_input_state_stack.push(GIS_CARD_PARAMS);
					select_card(&program->anim_state.card_anim_state, ui_mouse_over.card.card_id);
					program->action_being_built.type = ACTION_PLAY_CARD;
					program->action_being_built.entity_id = get_player(&program->game)->id;
					program->action_being_built.play_card.card_id = ui_mouse_over.card.card_id;
					program->action_being_built.play_card.action_type = action_type;
					program->action_being_built.play_card.params = program->card_params;
				} else {
					player_action.type = ACTION_PLAY_CARD;
					player_action.play_card.card_id = ui_mouse_over.card.card_id;
					player_action.play_card.action_type = action_type;
					player_action.play_card.params = program->card_params;
				}
				break;
			}
			case UI_MOUSE_OVER_NOTHING:
			case UI_MOUSE_OVER_ENTITY:
			case UI_MOUSE_OVER_DISCARD:
				player_action.type = ACTION_DISCARD_HAND;
				ASSERT(program->program_input_state_stack.peek() == GIS_PLAYING_CARDS);
				program->program_input_state_stack.pop();
				break;
			}
		}
		break;
	}

	case GIS_CARD_PARAMS: {
		if (input->num_presses(INPUT_BUTTON_MOUSE_RIGHT)) {
			program->program_input_state_stack.pop();
			unselect_card(&program->anim_state.card_anim_state);
			break;
		}
		auto cur_param = &program->card_params[program->cur_card_param];

		switch (cur_param->type) {
		case CARD_PARAM_TARGET:
			Pos pos = {};
			switch (ui_mouse_over.type) {
			case UI_MOUSE_OVER_NOTHING:
				highlight_pos(&program->anim_state, ui_mouse_over.world_pos, v4(1.0f, 0.0f, 0.0f, 1.0f));
				if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
					pos = ui_mouse_over.world_pos;
				}
				break;
			case UI_MOUSE_OVER_ENTITY: {
				highlight_entity(&program->anim_state, ui_mouse_over.entity.entity_id, v4(1.0f, 0.0f, 0.0f, 1.0f));
				if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
					pos = get_pos(&program->game, ui_mouse_over.entity.entity_id);
				}
				break;
			}
			}
			if (pos) {
				cur_param->target.dest = pos;
				++program->cur_card_param;
			}
			break;
		case CARD_PARAM_CREATURE: {
			switch (ui_mouse_over.type) {
			case UI_MOUSE_OVER_ENTITY:
				highlight_entity(&program->anim_state, ui_mouse_over.entity.entity_id, v4(1.0f, 0.0f, 0.0f, 1.0f));
				if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
					cur_param->creature.id = ui_mouse_over.entity.entity_id;
					++program->cur_card_param;
				}
				break;
			}
			break;
		}
		case CARD_PARAM_AVAILABLE_TILE: {
			Pos pos = {};
			auto player = get_player(&program->game);
			switch (ui_mouse_over.type) {
			case UI_MOUSE_OVER_NOTHING: {
				if (is_pos_passable(&program->game, ui_mouse_over.world_pos, player->movement_type)) {
					pos = ui_mouse_over.world_pos;
				}
				break;
			}
			case UI_MOUSE_OVER_ENTITY: {
				pos = get_pos(&program->game, ui_mouse_over.entity.entity_id);
				break;
			}
			}
			if (pos) {
				highlight_pos(&program->anim_state, pos, v4(1.0f, 0.0f, 0.0f, 1.0f));
				if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
					cur_param->available_tile.dest = pos;
					++program->cur_card_param;
				}
			}
			break;
		}
		}

		if (program->cur_card_param >= program->card_params.len) {
			player_action = program->action_being_built;
			ASSERT(player_action.type == ACTION_PLAY_CARD);
			program->program_input_state_stack.pop();
		}

		break;
	}

	case GIS_DRAGGING_MAP:
	case GIS_ANIMATING:
		break;
	}

	// simulate game
	if (player_action) {
		Max_Length_Array<Event, MAX_EVENTS> events;
		events.reset();
		do_action(&program->game, player_action, events);
		build_animations(&program->anim_state, events, time);
		program->program_input_state_stack.push(GIS_ANIMATING);
	}

	// render
	draw(&program->anim_state, program->render, &program->sound, screen_size, time);

	Entity_ID sprite_id = 0;
	if (ui_mouse_over.type == UI_MOUSE_OVER_ENTITY) {
		sprite_id = ui_mouse_over.entity.entity_id;
	}
	Card_ID selected_card_id = 0;
	if (ui_mouse_over.type == UI_MOUSE_OVER_CARD) {
		selected_card_id = ui_mouse_over.card.card_id;
	}

	// imgui
	IMGUI_Context *ic = &program->draw->imgui;
	imgui_begin(ic, input);
	if (program->display_debug_ui) {
		imgui_set_text_cursor(ic, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
		if (imgui_tree_begin(ic, "show mouse info")) {
			imgui_text(ic, "Mouse Pos: (%u, %u)", input->mouse_pos.x, input->mouse_pos.y);
			imgui_text(ic, "Mouse Delta: (%d, %d)", input->mouse_delta.x, input->mouse_delta.y);
			Pos sprite_pos = Pos(0, 0);
			{
				u32 num_entities = program->game.entities.len;
				Entity *entities = program->game.entities.items;
				for (u32 i = 0; i < num_entities; ++i) {
					Entity *e = &entities[i];
					if (e->id == sprite_id) {
						sprite_pos = e->pos;
						break;
					}
				}
			}
			imgui_text(ic, "world_pos: (%u, %u)", ui_mouse_over.world_pos.x, ui_mouse_over.world_pos.y);
			imgui_text(ic, "world_pos_pixels: (%f, %f)", ui_mouse_over.world_pos_pixels.x, ui_mouse_over.world_pos_pixels.y);
			imgui_text(ic, "ui_mouse_over.type: %u", ui_mouse_over.type);
			if (ui_mouse_over.type == UI_MOUSE_OVER_CARD) {
				imgui_text(ic, "ui_mouse_over.card.card_id = %u", ui_mouse_over.card.card_id);
			}
			imgui_text(ic, "Sprite ID: %u, position: (%u, %u)", sprite_id,
			           sprite_pos.x, sprite_pos.y);
			// imgui_text(ic, "Card mouse pos: (%f, %f)", card_mouse_pos.x, card_mouse_pos.y);
			imgui_text(ic, "Selected Card ID: %u", selected_card_id);
			imgui_text(ic, "World Center: (%f, %f)", program->anim_state.camera.world_center.x,
			           program->anim_state.camera.world_center.y);
			imgui_text(ic, "Zoom: %f", program->anim_state.camera.zoom);
			// draw input state stack
			{
				imgui_text(ic, "Program Input State Stack:");
				auto& state_stack = program->program_input_state_stack;
				for (u32 i = 0; i < state_stack.top; ++i) {
					imgui_text(ic, "    State: %s",
					           PROGRAM_INPUT_STATE_NAMES[state_stack.items[i]]);
				}
			}
			imgui_tree_end(ic);
		}
		if (imgui_tree_begin(ic, "cards")) {
			if (imgui_button(ic, "random 100")) {
				build_deck_random_n(program, 100);
			}
			if (imgui_button(ic, "random 20")) {
				build_deck_random_n(program, 20);
			}
			if (imgui_button(ic, "random 17")) {
				build_deck_random_n(program, 17);
			}
			if (imgui_button(ic, "poison")) {
				build_deck_poison(program);
			}
			if (imgui_button(ic, "lightning")) {
				build_lightning_deck(program);
			}
			imgui_tree_end(ic);
		}
		constants_do_imgui(ic);
		imgui_tree_end(ic);
	}

	// render world
	/*
	if (program->display_fog_of_war) {
		render(&program->game.fovs[0], program->render);
	} else {
		Field_Of_Vision fov = {};
		for (u32 y = 1; y < 255; ++y) {
			for (u32 x = 1; x < 255; ++x) {
				fov[Pos(x, y)] = FOV_VISIBLE;
			}
		}
		render(&fov, program->render);
	}
	*/

	{
		auto r = &program->render->render_job_buffer;
		begin(r, RENDER_EVENT_FOV_COMPOSITE);

		Render_Job job = {};
		job.type = RENDER_JOB_FOV_COMPOSITE;
		job.fov_composite.fov_id           = TARGET_TEXTURE_FOV_RENDER;
		job.fov_composite.world_static_id  = TARGET_TEXTURE_WORLD_STATIC;
		job.fov_composite.world_dynamic_id = TARGET_TEXTURE_WORLD_DYNAMIC;
		job.fov_composite.output_tex_id    = TARGET_TEXTURE_WORLD_COMPOSITE;
		push(r, job);

		end(r, RENDER_EVENT_FOV_COMPOSITE);
	}

	if (sprite_id) {
		highlight_sprite_id(&program->render->render_job_buffer,
		                    TARGET_TEXTURE_WORLD_COMPOSITE,
		                    TARGET_TEXTURE_SPRITE_ID,
		                    sprite_id,
		                    v4(1.0f, 0.0f, 0.0f, 1.0f));
	}

	// do sprite tooltip
	if (sprite_id && sprite_id < MAX_ENTITIES) {
		// XXX
		program->draw->boxy_bold.instances.reset();

		Entity *e = get_entity_by_id(&program->game, sprite_id);
		if (e) {
			char *buffer = fmt("%d/%d", e->hit_points, e->max_hit_points);

			u32 width = 1, height = 0;
			for (u8 *p = (u8*)buffer; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				width += glyph.dimensions.w - 1;
				height = max(height, glyph.dimensions.h);
			}

			Sprite_Sheet_Font_Instance instance = {};
			instance.world_pos = (v2)e->pos + v2(0.5f, -0.25f);
			instance.world_offset = { -(f32)(width / 2), -(f32)(height / 2) };
			instance.zoom = 1.0f;
			instance.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			for (u8 *p = (u8*)buffer; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				instance.glyph_pos = (v2)glyph.top_left;
				instance.glyph_size = (v2)glyph.dimensions;

				sprite_sheet_font_instances_add(&program->draw->boxy_bold, instance);

				instance.world_offset.x += (f32)glyph.dimensions.w - 1.0f;
			}
		}

		Render_Push_World_Font_Desc desc = {};
		desc.output_tex_id = TARGET_TEXTURE_WORLD_COMPOSITE;
		desc.font_tex_id = SOURCE_TEXTURE_BOXY_BOLD;
		desc.constants.screen_size = v2(256.0f*24.0f, 256.0f*24.0f);
		desc.constants.sprite_size = v2(24.0f, 24.0f);
		desc.constants.world_tile_size = v2(24.0f, 24.0f);
		desc.constants.tex_size = v2(1.0f, 1.0f);
		desc.instances = program->draw->boxy_bold.instances;
		push_world_font(&program->render->render_job_buffer, &desc);
	}

	// debug draw world
	// if (program->program_input_state_stack.peek() == GIS_NONE) {
	if (false) {
		Dijkstra_Map map;
		Map_Cache_Bool can_pass = {};

		auto player = get_player(&program->game);
		for (u32 y = 0; y < 256; ++y) {
			for (u32 x = 0; x < 256; ++x) {
				Pos p = Pos(x, y);
				if (is_pos_passable(&program->game, p, player->movement_type)) {
					can_pass.set(p);
				}
			}
		}

		calc_dijkstra_map(&can_pass, player->pos, &map);
		debug_draw_dijkstra_map(&map);

		v2 world_tl = screen_pos_to_world_pos(&program->anim_state.camera,
						screen_size,
						v2_u32(0, 0));
		v2 world_br = screen_pos_to_world_pos(&program->anim_state.camera,
						screen_size,
						screen_size);
		Debug_Draw_World_Constant_Buffer constants = {};
		constants.zoom = v2(256.0f, 256.0f);
		constants.center = v2(-128.0f, -128.0f);

		Render_Job job = {};
		job.type = RENDER_JOB_DDW_TRIANGLES;
		job.ddw_triangles.output_tex_id = TARGET_TEXTURE_WORLD_COMPOSITE;
		job.ddw_triangles.constants = constants;
		job.ddw_triangles.start = 0;
		job.ddw_triangles.count = program->draw->debug_draw_world.triangles.len;

		// XXX - memcpy to the instance buffer for now
		memcpy(program->render->render_job_buffer.instance_buffers[INSTANCE_BUFFER_DDW_TRIANGLE].base,
		       program->draw->debug_draw_world.triangles.items,
		       sizeof(program->draw->debug_draw_world.triangles.items[0]) * job.ddw_triangles.count);

		push(&program->render->render_job_buffer, job);

		// XXX - same thing
		Render_Job lines_job = {};
		lines_job.type = RENDER_JOB_DDW_LINES;
		lines_job.ddw_lines.output_tex_id = TARGET_TEXTURE_WORLD_COMPOSITE;
		lines_job.ddw_lines.constants = constants;
		lines_job.ddw_lines.start = 0;
		lines_job.ddw_lines.count = program->draw->debug_draw_world.lines.len;

		memcpy(program->render->render_job_buffer.instance_buffers[INSTANCE_BUFFER_DDW_LINE].base,
		       program->draw->debug_draw_world.lines.items,
		       sizeof(program->draw->debug_draw_world.lines.items[0]) * lines_job.ddw_lines.count);

		push(&program->render->render_job_buffer, lines_job);
	}

	// pixel art upsample
	{
		v2 world_tl = screen_pos_to_world_pos(&program->anim_state.camera, screen_size, { 0, 0 });
		v2 world_br = screen_pos_to_world_pos(&program->anim_state.camera, screen_size, screen_size);
		v2 input_size = world_br - world_tl;

		Render_Job job = {};
		job.type = RENDER_JOB_PIXEL_ART_UPSAMPLE;
		job.upsample_pixel_art.input_tex_id = TARGET_TEXTURE_WORLD_COMPOSITE;
		// job.upsample_pixel_art.output_tex_id = ...
		job.upsample_pixel_art.constants.input_size = input_size;
		job.upsample_pixel_art.constants.output_size = (v2)screen_size;
		job.upsample_pixel_art.constants.input_offset = world_tl;
		job.upsample_pixel_art.constants.output_offset = v2(0.0f, 0.0f);

		push(&program->render->render_job_buffer, job);
	}

	// XXX
	{
		Render_Job job = {};
		job.type = RENDER_JOB_XXX_FLUSH_OLD_RENDERER;

		push(&program->render->render_job_buffer, job);
	}

	// render user interface
	if (program->display_debug_ui) {
		render(ic, program->render);
	}
	if (program->is_console_visible) {
		render(&program->console, program->render, time);
	}
}

struct Process_Frame_Aux_Thread_Args
{
	Program* program;
	Input*   input;
	v2_u32   screen_size;
};

void process_frame_aux_thread(void* uncast_args)
{
	Program* program = (Program*)uncast_args;
	global_program = program;
	process_frame_aux(program, program->cur_input, program->cur_screen_size);
	auto orig_value = interlocked_compare_exchange(&program->process_frame_signal, 1, 0);
	ASSERT(!orig_value);
}

void process_frame(Program* program, Input* input, v2_u32 screen_size)
{
	set_global_state(program);

	switch (program->state) {
	case PROGRAM_STATE_NORMAL: {
		program->cur_input       = input;
		program->cur_screen_size = screen_size;
		start_thread(process_frame_aux_thread, "process_frame_aux", program);
		while (!interlocked_compare_exchange(&program->process_frame_signal, 0, 1)) {
			sleep(0);
		}
		break;
	}
	case PROGRAM_STATE_DEBUG_PAUSE:
		switch (program->program_input_state_stack.peek()) {
		case GIS_NONE:
			if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_ENDED_DOWN) {
				program->program_input_state_stack.push(GIS_DRAGGING_MAP);
			}
			break;
		case GIS_DRAGGING_MAP:
			if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
			  & INPUT_BUTTON_FLAG_HELD_DOWN) {
				f32 raw_zoom = (f32)screen_size.y / program->anim_state.camera.zoom;
				program->anim_state.camera.world_center -= (v2)input->mouse_delta / raw_zoom;
			} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
				     & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
				program->program_input_state_stack.pop();
			}
			break;
		}
		program->anim_state.camera.zoom -= (f32)input->mouse_wheel_delta * 0.25f;
		if (input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			program->state = PROGRAM_STATE_NORMAL;
			interlocked_compare_exchange(&program->debug_resume, 1, 0);
			while (!interlocked_compare_exchange(&program->process_frame_signal, 0, 1)) {
				sleep(0);
			}
		} else {
			imgui_begin(&program->draw->imgui, input);
			imgui_set_text_cursor(&program->draw->imgui,
			                      { 1.0f, 0.0f, 1.0f, 1.0f },
			                      { 5.0f, 5.0f });
			imgui_text(&program->draw->imgui, "DEBUG PAUSE");
		}
		break;
	}
}