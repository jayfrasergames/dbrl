#include "dbrl.h"

#include "jfg/imgui.h"
#include "jfg/jfg_math.h"
#include "jfg/log.h"
#include "sprite_sheet.h"
#include "pixel_art_upsampler.h"

#include <stdio.h>  // XXX - for snprintf
#include <stdlib.h> // XXX - for random

#include "gen/appearance.data.h"
#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"
#include "gen/sprite_sheet_water_edges.data.h"

#include "gen/pass_through_dxbc_vertex_shader.data.h"
#include "gen/pass_through_dxbc_pixel_shader.data.h"

// =============================================================================
// type definitions/constants

typedef v2_u8 Pos;

#define MAX_ENTITIES 10240

typedef u16 Entity_ID;

#define ANIM_MOVE_DURATION 0.5f

#define Z_OFFSET_FLOOR            0.0f
#define Z_OFFSET_WALL             1.0f
#define Z_OFFSET_WALL_SHADOW      0.1f
#define Z_OFFSET_CHARACTER        1.0f
#define Z_OFFSET_CHARACTER_SHADOW 0.9f
#define Z_OFFSET_WATER_EDGE       0.05f

// =============================================================================
// game

enum Brain_Type
{
	BRAIN_TYPE_NONE,
	BRAIN_TYPE_PLAYER,
	BRAIN_TYPE_RANDOM,
};

struct Brain
{
	Brain_Type type;
};

#define MAX_CHOICES 32

enum Choice_Type
{
	CHOICE_START_TURN,
};

struct Choice
{
	Choice_Type type;
	Entity_ID   entity_id;
};

enum Action_Type
{
	ACTION_NONE,
	ACTION_MOVE,
	ACTION_WAIT,
};

struct Action
{
	Action_Type type;
	union {
		struct {
			Entity_ID entity_id;
			Pos start, end;
		} move;
	};
};

// Transactions

enum Event_Type
{
	EVENT_NONE,
	EVENT_MOVE,
};

struct Event
{
	Event_Type type;
	f32 time;
	u32 block_id;
	union {
		struct {
			Entity_ID entity_id;
			Pos start, end;
		} move;
	};
};

#define MAX_EVENTS 10240
struct Event_Buffer
{
	u32   num_events;
	Event events[MAX_EVENTS];
};

u8 event_type_is_blocking(Event_Type type)
{
	switch (type) {
	case EVENT_NONE: return 0;
	case EVENT_MOVE: return 0;
	}
	ASSERT(0);
	return 1;
}

enum Block_Flag
{
	BLOCK_WALK = 1 << 0,
	BLOCK_SWIM = 1 << 1,
	BLOCK_FLY  = 1 << 2,
};

struct Entity
{
	Entity_ID  id;
	u16        movement_type;
	u16        block_mask;
	Pos        pos;
	Appearance appearance;
	Brain      brain;
};

// maybe better called "world state"?
struct Game
{
	u32 current_entity;
	u32 cur_block_id;
	f32 block_time;
	u32 num_entities;
	Entity entities[MAX_ENTITIES];
	u32 choice_stack_idx;
	Choice choice_stack[MAX_CHOICES];
};

void game_build_from_string(Game* game, char* str)
{
	Pos cur_pos = { 1, 1 };
	u32 idx = 0;
	Entity_ID e_id = 1;
	for (char *p = str; *p; ++p) {
		switch (*p) {
		case '\n':
			cur_pos.x = 1;
			++cur_pos.y;
			continue;
		case '#': {
			Entity e = {};
			e.id = e_id++;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_WALL_WOOD;
			e.brain.type = BRAIN_TYPE_NONE;
			game->entities[idx++] = e;
			break;
		}
		case 'x': {
			Entity e = {};
			e.id = e_id++;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_WALL_FANCY;
			e.brain.type = BRAIN_TYPE_NONE;
			game->entities[idx++] = e;
			break;
		}
		case '.': {
			Entity e = {};
			e.id = e_id++;
			e.block_mask = BLOCK_SWIM;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_FLOOR_ROCK;
			e.brain.type = BRAIN_TYPE_NONE;
			game->entities[idx++] = e;
			break;
		}
		case '@': {
			Entity e = {};
			e.id = e_id++;
			e.block_mask = BLOCK_SWIM;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_FLOOR_ROCK;
			e.brain.type = BRAIN_TYPE_NONE;
			game->entities[idx++] = e;
			e.id = e_id++;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.appearance = APPEARANCE_CREATURE_MALE_BERSERKER;
			e.movement_type = BLOCK_WALK;
			e.brain.type = BRAIN_TYPE_PLAYER;
			game->entities[idx++] = e;
			break;
		}
		case 'b': {
			Entity e = {};
			e.id = e_id++;
			e.block_mask = BLOCK_SWIM;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_FLOOR_ROCK;
			e.brain.type = BRAIN_TYPE_NONE;
			game->entities[idx++] = e;
			e.id = e_id++;
			e.movement_type = BLOCK_FLY;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.appearance = APPEARANCE_CREATURE_RED_BAT;
			e.brain.type = BRAIN_TYPE_RANDOM;
			game->entities[idx++] = e;
			break;
		}
		case '~': {
			Entity e = {};
			e.id = e_id++;
			e.block_mask = BLOCK_WALK;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_LIQUID_WATER;
			e.brain.type = BRAIN_TYPE_NONE;
			game->entities[idx++] = e;
			break;
		}
		}
		++cur_pos.x;
	}
	game->num_entities = idx;
}

Entity* game_get_entity_by_id(Game* game, Entity_ID entity_id)
{
	for (u32 i = 0; i < game->num_entities; ++i) {
		if (game->entities[i].id == entity_id) {
			return &game->entities[i];
		}
	}
	return NULL;
}

u8 game_is_passable(Game* game, Pos pos, u16 block_flag)
{
	u32 num_entities = game->num_entities;
	Entity *e = game->entities;
	u8 result = 0;
	for (u32 i = 0; i < num_entities; ++i, ++e) {
		if (e->pos.x == pos.x && e->pos.y == pos.y) {
			if (e->block_mask & block_flag) {
				return 0;
			}
			result = 1;
		}
	}
	return result;
}

u8 game_do_action(Game* game, Action action, Event_Buffer* event_buffer)
{
	// maybe assert that the action matches the current choice?
	switch (action.type) {
	case ACTION_NONE:
		ASSERT(0);
		return 1;
	case ACTION_MOVE: {
		i8 dx = action.move.end.x - action.move.start.x;
		i8 dy = action.move.end.y - action.move.start.y;
		if (dx * dx > 1 || dy * dy > 1) {
			return 0;
		}
		Entity *e;
		for (u32 i = 0; i < game->num_entities; ++i) {
			e = &game->entities[i];
			if (e->id == action.move.entity_id) {
				break;
			}
		}
		ASSERT(e->id == action.move.entity_id);
		ASSERT(e->pos.x == action.move.start.x && e->pos.y == action.move.start.y);
		if (!game_is_passable(game, action.move.end, e->movement_type)) {
			return 0;
		}
		e->pos = action.move.end;
		Event event = {};
		event.time = game->block_time;
		event.block_id = game->cur_block_id;
		event.type = EVENT_MOVE;
		event.move.entity_id = action.move.entity_id;
		event.move.start = action.move.start;
		event.move.end = action.move.end;
		event_buffer->events[event_buffer->num_events++] = event;
		--game->choice_stack_idx;
		return 1;
	}
	case ACTION_WAIT:
		--game->choice_stack_idx;
		return 1;
	}
	return 0;
}

void game_play_until_input_required(Game* game, Event_Buffer* event_buffer)
{
	u32 cur_entity = game->current_entity;
	for (;;) {
		Entity *e = &game->entities[cur_entity];
		if (game->choice_stack_idx) {
			switch (e->brain.type) {
			case BRAIN_TYPE_NONE:
				ASSERT(0);
				break;
			case BRAIN_TYPE_PLAYER:
				goto need_player_input;
			case BRAIN_TYPE_RANDOM: {
				Choice choice = game->choice_stack[game->choice_stack_idx - 1];
				ASSERT(choice.type == CHOICE_START_TURN);
				Action action = {};
				u32 num_poss = 0;
				Pos poss[8] = {};
				for (i8 dy = -1; dy <= 1; ++dy) {
					for (i8 dx = -1; dx <= 1; ++dx) {
						Pos new_pos = {};
						new_pos.x = e->pos.x + dx;
						new_pos.y = e->pos.y + dy;
						if (game_is_passable(game, new_pos, BLOCK_FLY)) {
							poss[num_poss++] = new_pos;
						}
					}
				}
				if (num_poss) {
					action.type = ACTION_MOVE;
					action.move.entity_id = choice.entity_id;
					action.move.start = e->pos;
					action.move.end = poss[rand() % num_poss];
				} else {
					Action action = {};
					action.type = ACTION_WAIT;
				}
				u8 did_action = game_do_action(game, action, event_buffer);
				ASSERT(did_action);
				break;
			}
			}
		}

		// advance to next turn
		++game->cur_block_id;
		game->block_time = 0.0f;
		do {
			cur_entity = (cur_entity + 1) % game->num_entities;
			e = &game->entities[cur_entity];
		} while (!e->brain.type);
		Choice choice = {};
		choice.type = CHOICE_START_TURN;
		choice.entity_id = e->id;
		game->choice_stack[0] = choice;
		game->choice_stack_idx = 1;
	}
need_player_input:
	game->current_entity = cur_entity;
}

// =============================================================================
// draw

struct Camera
{
	v2  world_center;
	f32 zoom;
};

struct Draw
{
	Camera camera;
	Sprite_Sheet_Renderer  renderer;
	Sprite_Sheet_Instances tiles;
	Sprite_Sheet_Instances creatures;
	Sprite_Sheet_Instances water_edges;
};

// =============================================================================
// anim

enum Anim_Type
{
	ANIM_TILE_STATIC,
	ANIM_WATER_EDGE,
	ANIM_CREATURE_IDLE,
	ANIM_MOVE,
};

struct Anim
{
	Anim_Type type;
	union {
		struct {
			f32 duration;
			f32 offset;
		} idle;
		struct {
			v4_u8 color;
		} water_edge;
		struct {
			f32 duration;
			f32 start_time;
			v2 start;
			v2 end;
		} move;
	};
	v2 sprite_coords;
	v2 world_coords;
	Entity_ID entity_id;
	f32 depth_offset;
};

#define MAX_ANIMS (5 * MAX_ENTITIES)
#define MAX_ANIM_BLOCKS 1024

struct World_Anim_State
{
	u32          num_anims;
	u32          num_active_anims;
	Anim         anims[MAX_ANIMS];
	f32          dynamic_anim_start_time;
	u32          anim_block_number;
	u32          total_anim_blocks;
	u32          event_buffer_idx;
	Event_Buffer events_to_be_animated;
};

void world_anim_build_events_to_be_animated(World_Anim_State* world_anim, Event_Buffer* event_buffer)
{
	u32 num_events = event_buffer->num_events;
	u32 cur_block_number = 0;
	Event_Buffer *dest = &world_anim->events_to_be_animated;
	u8 prev_event_is_blocking = 1;
	for (u32 i = 0; i < num_events; ++i) {
		Event *cur_event = &event_buffer->events[i];
		u8 create_new_block = prev_event_is_blocking || event_type_is_blocking(cur_event->type);
		prev_event_is_blocking = event_type_is_blocking(cur_event->type);
		if (create_new_block) {
			++cur_block_number;
		}
		dest->events[i] = *cur_event;
		dest->events[i].block_id = cur_block_number;
	}
	world_anim->anim_block_number = 1;
	world_anim->total_anim_blocks = cur_block_number;
	world_anim->event_buffer_idx = 0;
	dest->num_events = num_events;
}

void world_anim_animate_next_event_block(World_Anim_State* world_anim)
{
	u32 num_anims = world_anim->num_anims;
	u32 event_idx = world_anim->event_buffer_idx;
	u32 num_events = world_anim->events_to_be_animated.num_events;
	u32 cur_block_id = world_anim->anim_block_number++;
	Event *events = world_anim->events_to_be_animated.events;

	while (event_idx < num_events && events[event_idx].block_id == cur_block_id) {
		Event *event = &events[event_idx];
		switch (event->type) {
		case EVENT_MOVE:
			for (u32 i = 0; i < num_anims; ++i) {
				Anim *anim = &world_anim->anims[i];
				if (anim->entity_id == event->move.entity_id) {
					anim->type = ANIM_MOVE;
					anim->move.duration = ANIM_MOVE_DURATION;
					anim->move.start_time = event->time;
					anim->move.start.x = (f32)event->move.start.x;
					anim->move.start.y = (f32)event->move.start.y;
					anim->move.end.x   = (f32)event->move.end.x;
					anim->move.end.y   = (f32)event->move.end.y;
					++world_anim->num_active_anims;
				}
			}
			break;
		}
		++event_idx;
	}

	world_anim->event_buffer_idx = event_idx;
}

u8 world_anim_is_animating(World_Anim_State* world_anim)
{
	return world_anim->num_active_anims
	    || world_anim->anim_block_number < world_anim->total_anim_blocks;
}

void world_anim_do_events(World_Anim_State* world_anim, Event_Buffer* event_buffer, f32 time)
{
	u32 num_anims = world_anim->num_anims;
	u32 num_events = event_buffer->num_events;
	for (u32 i = 0; i < num_events; ++i) {
		Event *event = &event_buffer->events[i];
		switch (event->type) {
		case EVENT_MOVE:
			for (u32 i = 0; i < num_anims; ++i) {
				Anim *anim = &world_anim->anims[i];
				if (anim->entity_id == event->move.entity_id) {
					anim->type = ANIM_MOVE;
					anim->move.duration = ANIM_MOVE_DURATION;
					anim->move.start_time = time;
					anim->move.start.x = (f32)event->move.start.x;
					anim->move.start.y = (f32)event->move.start.y;
					anim->move.end.x   = (f32)event->move.end.x;
					anim->move.end.y   = (f32)event->move.end.y;
				}
			}
			break;
		}
	}
}

void world_anim_init(World_Anim_State* world_anim, Game* game)
{
	u32 anim_idx = 0;

	u8 wall_id_grid[65536];
	memset(wall_id_grid, 0, sizeof(wall_id_grid));
	Entity_ID wall_entity_id_grid[65536];

	u8 liquid_id_grid[65536];
	memset(liquid_id_grid, 0, sizeof(liquid_id_grid));
	Entity_ID liquid_entity_id_grid[65536];

	for (u32 i = 0; i < game->num_entities; ++i) {
		Entity *e = &game->entities[i];
		Appearance app = e->appearance;
		if (!app) {
			continue;
		}
		Pos pos = e->pos;
		if (appearance_is_creature(app)) {
			Anim ca = {};
			ca.type = ANIM_CREATURE_IDLE;
			ca.sprite_coords = appearance_get_creature_sprite_coords(app);
			ca.world_coords = { (f32)pos.x, (f32)pos.y };
			ca.entity_id = e->id;
			ca.depth_offset = Z_OFFSET_CHARACTER;
			ca.idle.duration = 0.8f + 0.4f * ((f32)rand() / (f32)RAND_MAX);
			ca.idle.offset = 0.0f;
			world_anim->anims[anim_idx++] = ca;
			continue;
		}
		if (appearance_is_floor(app)) {
			Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_floor_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = Z_OFFSET_FLOOR;
			world_anim->anims[anim_idx++] = ta;
			continue;
		}
		if (appearance_is_wall(app)) {
			u8 wall_id = appearance_get_wall_id(app);
			u32 index = pos.y * 256 + pos.x;
			wall_id_grid[index] = wall_id;
			wall_entity_id_grid[index] = e->id;
			continue;
		}
		if (appearance_is_liquid(app)) {
			Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_liquid_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = Z_OFFSET_FLOOR;
			world_anim->anims[anim_idx++] = ta;
			u32 index = pos.y * 256 + pos.x;
			liquid_id_grid[index] = appearance_get_liquid_id(app);
			liquid_entity_id_grid[index] = e->id;
		}
	}

	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			u32 index = y*256 + x;
			u8 c = wall_id_grid[index];
			if (!c) {
				continue;
			}
			Appearance app = appearance_wall_id_to_appearance(c);

			// get whether the floor ids equal c in the following pattern
			//  tl | t | tr
			// ----+---+----
			//  l  | c | r
			// ----+---+----
			//  bl | b | br

			u8 tl = wall_id_grid[index - 257] == c;
			u8 t  = wall_id_grid[index - 256] == c;
			u8 tr = wall_id_grid[index - 255] == c;
			u8 l  = wall_id_grid[index - 1]  == c;
			u8 r  = wall_id_grid[index + 1]  == c;
			u8 bl = wall_id_grid[index + 255] == c;
			u8 b  = wall_id_grid[index + 256] == c;
			u8 br = wall_id_grid[index + 257] == c;

			u8 connection_mask = 0;
			if (t && !(tl && tr && l && r)) {
				connection_mask |= APPEARANCE_N;
			}
			if (r && !(t && tr && b && br)) {
				connection_mask |= APPEARANCE_E;
			}
			if (b && !(l && r && bl && br)) {
				connection_mask |= APPEARANCE_S;
			}
			if (l && !(t && b && tl && bl)) {
				connection_mask |= APPEARANCE_W;
			}

			Anim anim = {};
			anim.type = ANIM_TILE_STATIC;
			anim.sprite_coords = appearance_get_wall_sprite_coords(app, connection_mask);
			anim.world_coords = { (f32)x, (f32)y };
			anim.entity_id = wall_entity_id_grid[index];
			anim.depth_offset = Z_OFFSET_WALL;
			world_anim->anims[anim_idx++] = anim;

			if (!wall_id_grid[index + 256]) {
				anim.sprite_coords = { 30.0f, 36.0f };
				anim.world_coords = { (f32)x, (f32)y + 1.0f };
				anim.depth_offset = Z_OFFSET_WALL_SHADOW;
				world_anim->anims[anim_idx++] = anim;
			}
		}
	}

	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			u32 index = y*256 + x;
			u8 c = liquid_id_grid[index];

			if (!c) {
				continue;
			}

			u8 mask = 0;
			if (liquid_id_grid[index - 256] == c) { mask |= 0x01; }
			if (liquid_id_grid[index - 255] == c) { mask |= 0x02; }
			if (liquid_id_grid[index +   1] == c) { mask |= 0x04; }
			if (liquid_id_grid[index + 257] == c) { mask |= 0x08; }
			if (liquid_id_grid[index + 256] == c) { mask |= 0x10; }
			if (liquid_id_grid[index + 255] == c) { mask |= 0x20; }
			if (liquid_id_grid[index -   1] == c) { mask |= 0x40; }
			if (liquid_id_grid[index - 257] == c) { mask |= 0x80; }
			if (~mask & 0xFF) {
				Anim anim = {};
				anim.type = ANIM_WATER_EDGE;
				anim.sprite_coords = { (f32)(mask % 16), (f32)(15 - mask / 16) };
				anim.world_coords = { (f32)x, (f32)y };
				anim.entity_id = liquid_entity_id_grid[index];
				anim.depth_offset = Z_OFFSET_WATER_EDGE;
				anim.water_edge.color = { 0x58, 0x80, 0xc0, 255 };
				world_anim->anims[anim_idx++] = anim;
			}
		}
	}

	ASSERT(anim_idx < MAX_ANIMS);
	world_anim->num_anims = anim_idx;
}

void world_anim_draw(World_Anim_State* world_anim, Draw* draw, f32 time)
{
	// reset draw state
	sprite_sheet_instances_reset(&draw->tiles);
	sprite_sheet_instances_reset(&draw->creatures);
	sprite_sheet_instances_reset(&draw->water_edges);

	f32 dyn_time = time - world_anim->dynamic_anim_start_time;

	// clear up finished animations
	for (u32 i = 0; i < world_anim->num_anims; ++i) {
		Anim *anim = &world_anim->anims[i];
		switch (anim->type) {
		case ANIM_MOVE:
			if (anim->move.start_time + anim->move.duration <= dyn_time) {
				anim->world_coords = anim->move.end;
				anim->type = ANIM_CREATURE_IDLE;
				anim->idle.offset = time;
				anim->idle.duration = 0.8f + 0.4f * ((f32)rand()/(f32)RAND_MAX);
				--world_anim->num_active_anims;
			}
			break;
		}
	}

	// prepare next events if we're still animating
	if (world_anim->num_active_anims == 0
	    && world_anim->anim_block_number <= world_anim->total_anim_blocks) {
		world_anim_animate_next_event_block(world_anim);
		world_anim->dynamic_anim_start_time = time;
		dyn_time = 0.0f;
	}


	// draw tile animations
	for (u32 i = 0; i < world_anim->num_anims; ++i) {
		Anim *anim = &world_anim->anims[i];
		switch (anim->type) {
		case ANIM_TILE_STATIC: {
			Sprite_Sheet_Instance ti = {};
			ti.sprite_pos = anim->sprite_coords;
			ti.world_pos = anim->world_coords;
			ti.sprite_id = anim->entity_id;
			ti.depth_offset = anim->depth_offset;
			ti.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			sprite_sheet_instances_add(&draw->tiles, ti);
			break;
		}
		case ANIM_CREATURE_IDLE: {
			Sprite_Sheet_Instance ci = {};

			v2 world_pos = anim->world_coords;
			world_pos.y -= 3.0f / 24.0f;
			ci.sprite_pos = { 4.0f, 22.0f };
			ci.world_pos = world_pos;
			ci.sprite_id = anim->entity_id;
			ci.depth_offset = anim->depth_offset;
			ci.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			sprite_sheet_instances_add(&draw->creatures, ci);

			v2 sprite_pos = anim->sprite_coords;
			f32 dt = time + anim->idle.offset;
			dt = fmod(dt, anim->idle.duration) / anim->idle.duration;
			if (dt > 0.5f) {
				sprite_pos.y += 1.0f;
			}
			ci.sprite_pos = sprite_pos;
			world_pos.y -= 3.0f / 24.0f;
			ci.world_pos = world_pos;
			ci.depth_offset += 0.5f;

			sprite_sheet_instances_add(&draw->creatures, ci);
			break;
		}
		case ANIM_WATER_EDGE: {
			Sprite_Sheet_Instance water_edge = {};
			water_edge.sprite_pos = anim->sprite_coords;
			water_edge.world_pos = anim->world_coords;
			water_edge.sprite_id = anim->entity_id;
			water_edge.depth_offset = anim->depth_offset;
			v4_u8 c = anim->water_edge.color;
			water_edge.color_mod = {
				(f32)c.r / 256.0f,
				(f32)c.g / 256.0f,
				(f32)c.b / 256.0f,
				(f32)c.a / 256.0f,
			};
			sprite_sheet_instances_add(&draw->water_edges, water_edge);
			break;
		}
		case ANIM_MOVE: {
			f32 dt = (dyn_time - anim->move.start_time) / anim->move.duration;
			v2 start = anim->move.start;
			v2 end = anim->move.end;
			v2 world_pos = {};
			world_pos.x = start.x + (end.x - start.x) * dt;
			world_pos.y = start.y + (end.y - start.y) * dt;

			Sprite_Sheet_Instance ci = {};

			// draw shadow
			world_pos.y -= 3.0f / 24.0f;
			ci.sprite_pos = { 4.0f, 22.0f };
			ci.world_pos = world_pos;
			ci.sprite_id = anim->entity_id;
			ci.depth_offset = anim->depth_offset;
			ci.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			sprite_sheet_instances_add(&draw->creatures, ci);

			v2 sprite_pos = anim->sprite_coords;
			if (dt > 0.5f) {
				sprite_pos.y += 1.0f;
			}
			ci.sprite_pos = sprite_pos;
			world_pos.y -= 3.0f / 24.0f + 0.5f * dt*(1.0f - dt);
			ci.world_pos = world_pos;
			ci.depth_offset += 0.5f;

			sprite_sheet_instances_add(&draw->creatures, ci);

			break;
		}
		}
	}
}

// =============================================================================
// program

enum Program_Input_State
{
	GIS_NONE,
	GIS_DRAGGING_MAP,
};

struct Program
{
	Game                game;
	Program_Input_State program_input_state;
	World_Anim_State    world_anim;
	Draw                draw;

	u32           frame_number;
	v2            screen_size;
	v2_u32        max_screen_size;
	IMGUI_Context imgui;

	Pixel_Art_Upsampler    pixel_art_upsampler;
	union {
		struct {
			ID3D11Texture2D*           output_texture;
			ID3D11UnorderedAccessView* output_uav;
			ID3D11RenderTargetView*    output_rtv;
			ID3D11ShaderResourceView*  output_srv;
			ID3D11VertexShader*        output_vs;
			ID3D11PixelShader*         output_ps;
		} d3d11;
	};
};

Memory_Spec get_program_size()
{
	Memory_Spec result = {};
	result.alignment = alignof(Program);
	result.size = sizeof(Program);
	return result;
}

void program_init(Program* program)
{
	memset(program, 0, sizeof(*program));
	sprite_sheet_renderer_init(&program->draw.renderer,
	                           &program->draw.tiles, 3,
	                           { 1600, 900 });

	program->draw.tiles.data       = SPRITE_SHEET_TILES;
	program->draw.creatures.data   = SPRITE_SHEET_CREATURES;
	program->draw.water_edges.data = SPRITE_SHEET_WATER_EDGES;

	game_build_from_string(&program->game,
		"##########################################\n"
		"#.b.b.........#..........................#\n"
		"#..@...#......#..........................#\n"
		"#.....###.....#.........###..............#\n"
		"#....#####....#.........#x#..............#\n"
		"#...##.#.##...#.......###x#.#............#\n"
		"#..##..#..##..#.......#xxxxx#............#\n"
		"#......#......#.......###x###............#\n"
		"#......#......#.........#x#..............#\n"
		"#......#................###..............#\n"
		"#......#......#..........................#\n"
		"#......#......#............#.............#\n"
		"#......#......#..........................#\n"
		"#~~~...#.....x#..............#...........#\n"
		"#~~~~..#.....x#..........................#\n"
		"#~~~~~~..xxxxx#............#...#.........#\n"
		"##########x####..........................#\n"
		"#.........x...#........#.................#\n"
		"#.........x...#.......##.................#\n"
		"#.............#......##.##...............#\n"
		"#.............#.......##.................#\n"
		"#.............#........#.................#\n"
		"#........................~~..............#\n"
		"#.............#.........~~~~~............#\n"
		"#.............#.........~...~~~..........#\n"
		"#.............#.........~~.~~.~..........#\n"
		"#.............#............~.~...........#\n"
		"#.............#...........~~~~...........#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"##########################################\n");

	program->draw.camera.zoom = 4.0f;
	program->draw.camera.world_center = { 0.0f, 0.0f };

	Event_Buffer tmp_buffer = {};
	game_play_until_input_required(&program->game, &tmp_buffer);

	world_anim_init(&program->world_anim, &program->game);
}

u8 program_d3d11_init(Program* program, ID3D11Device* device, v2_u32 screen_size)
{
	HRESULT hr;
	ID3D11Texture2D *output_texture;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width              = screen_size.w;
		desc.Height             = screen_size.h;
		desc.MipLevels          = 1;
		desc.ArraySize          = 1;
		desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count   = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage              = D3D11_USAGE_DEFAULT;
		desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		                                                     | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags     = 0;
		desc.MiscFlags          = 0;

		hr = device->CreateTexture2D(&desc, NULL, &output_texture);
	}
	if (FAILED(hr)) {
		goto error_init_output_texture;
	}

	ID3D11UnorderedAccessView *output_uav;
	hr = device->CreateUnorderedAccessView(output_texture, NULL, &output_uav);
	if (FAILED(hr)) {
		goto error_init_output_uav;
	}

	ID3D11RenderTargetView *output_rtv;
	hr = device->CreateRenderTargetView(output_texture, NULL, &output_rtv);
	if (FAILED(hr)) {
		goto error_init_output_rtv;
	}

	ID3D11ShaderResourceView *output_srv;
	hr = device->CreateShaderResourceView(output_texture, NULL, &output_srv);
	if (FAILED(hr)) {
		goto error_init_output_srv;
	}

	ID3D11VertexShader *output_vs;
	hr = device->CreateVertexShader(PASS_THROUGH_VS, ARRAY_SIZE(PASS_THROUGH_VS), 0, &output_vs);
	if (FAILED(hr)) {
		goto error_init_output_vs;
	}

	ID3D11PixelShader *output_ps;
	hr = device->CreatePixelShader(PASS_THROUGH_PS, ARRAY_SIZE(PASS_THROUGH_PS), 0, &output_ps);
	if (FAILED(hr)) {
		goto error_init_output_ps;
	}

	if (!sprite_sheet_renderer_d3d11_init(&program->draw.renderer, device)) {
		goto error_init_sprite_sheet_renderer;
	}

	if (!sprite_sheet_instances_d3d11_init(&program->draw.tiles, device)) {
		goto error_init_sprite_sheet_tiles;
	}

	if (!sprite_sheet_instances_d3d11_init(&program->draw.creatures, device)) {
		goto error_init_sprite_sheet_creatures;
	}

	if (!sprite_sheet_instances_d3d11_init(&program->draw.water_edges, device)) {
		goto error_init_sprite_sheet_water_edges;
	}

	if (!pixel_art_upsampler_d3d11_init(&program->pixel_art_upsampler, device)) {
		goto error_init_pixel_art_upsampler;
	}

	if (!imgui_d3d11_init(&program->imgui, device)) {
		goto error_init_imgui;
	}

	program->max_screen_size      = screen_size;
	program->d3d11.output_texture = output_texture;
	program->d3d11.output_uav     = output_uav;
	program->d3d11.output_rtv     = output_rtv;
	program->d3d11.output_srv     = output_srv;
	program->d3d11.output_vs      = output_vs;
	program->d3d11.output_ps      = output_ps;

	return 1;

	imgui_d3d11_free(&program->imgui);
error_init_imgui:
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
error_init_pixel_art_upsampler:
	sprite_sheet_instances_d3d11_free(&program->draw.water_edges);
error_init_sprite_sheet_water_edges:
	sprite_sheet_instances_d3d11_free(&program->draw.creatures);
error_init_sprite_sheet_creatures:
	sprite_sheet_instances_d3d11_free(&program->draw.tiles);
error_init_sprite_sheet_tiles:
	sprite_sheet_renderer_d3d11_free(&program->draw.renderer);
error_init_sprite_sheet_renderer:
	output_ps->Release();
error_init_output_ps:
	output_vs->Release();
error_init_output_vs:
	output_srv->Release();
error_init_output_srv:
	output_rtv->Release();
error_init_output_rtv:
	output_uav->Release();
error_init_output_uav:
	output_texture->Release();
error_init_output_texture:
	return 0;
}

void program_d3d11_free(Program* program)
{
	imgui_d3d11_free(&program->imgui);
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
	sprite_sheet_instances_d3d11_free(&program->draw.water_edges);
	sprite_sheet_instances_d3d11_free(&program->draw.creatures);
	sprite_sheet_instances_d3d11_free(&program->draw.tiles);
	sprite_sheet_renderer_d3d11_free(&program->draw.renderer);
	program->d3d11.output_ps->Release();
	program->d3d11.output_vs->Release();
	program->d3d11.output_srv->Release();
	program->d3d11.output_rtv->Release();
	program->d3d11.output_uav->Release();
	program->d3d11.output_texture->Release();
}

u8 program_d3d11_set_screen_size(Program* program, ID3D11Device* device, v2_u32 screen_size)
{
	v2_u32 prev_max_screen_size = program->max_screen_size;
	if (screen_size.w < prev_max_screen_size.w && screen_size.h < prev_max_screen_size.h) {
		return 1;
	}
	screen_size.w = max_u32(screen_size.w, prev_max_screen_size.w);
	screen_size.h = max_u32(screen_size.h, prev_max_screen_size.h);

	HRESULT hr;
	ID3D11Texture2D *output_texture;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = screen_size.w;
		desc.Height = screen_size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
		                                            | D3D11_BIND_UNORDERED_ACCESS;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		hr = device->CreateTexture2D(&desc, NULL, &output_texture);
	}
	if (FAILED(hr)) {
		goto error_init_output_texture;
	}

	ID3D11UnorderedAccessView *output_uav;
	hr = device->CreateUnorderedAccessView(output_texture, NULL, &output_uav);
	if (FAILED(hr)) {
		goto error_init_output_uav;
	}

	ID3D11RenderTargetView *output_rtv;
	hr = device->CreateRenderTargetView(output_texture, NULL, &output_rtv);
	if (FAILED(hr)) {
		goto error_init_output_rtv;
	}

	ID3D11ShaderResourceView *output_srv;
	hr = device->CreateShaderResourceView(output_texture, NULL, &output_srv);
	if (FAILED(hr)) {
		goto error_init_output_srv;
	}

	program->d3d11.output_uav->Release();
	program->d3d11.output_rtv->Release();
	program->d3d11.output_srv->Release();
	program->d3d11.output_texture->Release();

	program->max_screen_size = screen_size;
	program->d3d11.output_uav = output_uav;
	program->d3d11.output_rtv = output_rtv;
	program->d3d11.output_srv = output_srv;
	program->d3d11.output_texture = output_texture;
	return 1;

	output_srv->Release();
error_init_output_srv:
	output_rtv->Release();
error_init_output_rtv:
	output_uav->Release();
error_init_output_uav:
	output_texture->Release();
error_init_output_texture:
	return 0;
}

static v2_i32 screen_pos_to_world_pos(Camera* camera, v2_u32 screen_size, v2_u32 screen_pos)
{
	v2_i32 world_pos = {};
	v2_i32 p = { (i32)screen_pos.x - (i32)screen_size.w / 2,
	             (i32)screen_pos.y - (i32)screen_size.h / 2 };
	world_pos.x = (i32)((f32)p.x / camera->zoom - 24.0f * camera->world_center.x);
	world_pos.y = (i32)((f32)p.y / camera->zoom - 24.0f * camera->world_center.y);
	return world_pos;
}

void process_frame(Program* program, Input* input, v2_u32 screen_size)
{
	program->screen_size = { (f32)screen_size.w, (f32)screen_size.h };
	++program->frame_number;

	// process input
	switch (program->program_input_state) {
	case GIS_NONE:
		if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_ENDED_DOWN) {
			program->program_input_state = GIS_DRAGGING_MAP;
		}
		break;
	case GIS_DRAGGING_MAP:
		if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_HELD_DOWN) {
			f32 zoom = 24.0f * program->draw.camera.zoom;
			program->draw.camera.world_center.x += (f32)input->mouse_delta.x / zoom;
			program->draw.camera.world_center.y += (f32)input->mouse_delta.y / zoom;
		} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
		             & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
			program->program_input_state = GIS_NONE;
		}
		break;
	}
	program->draw.camera.zoom += (f32)input->mouse_wheel_delta * 0.25f;

	f32 time = (f32)program->frame_number / 60.0f;
	world_anim_draw(&program->world_anim, &program->draw, time);

	v2_i32 world_mouse_pos = screen_pos_to_world_pos(&program->draw.camera,
	                                                 screen_size,
	                                                 input->mouse_pos);
	u32 sprite_id = sprite_sheet_renderer_id_in_pos(&program->draw.renderer,
	                                                { (u32)world_mouse_pos.x,
	                                                  (u32)world_mouse_pos.y });

	Event_Buffer event_buffer = {};

	if (!world_anim_is_animating(&program->world_anim)) {
		Choice current_choice = program->game.choice_stack[program->game.choice_stack_idx - 1];
		switch (current_choice.type) {
		case CHOICE_START_TURN: {
			Input_Button_Frame_Data lmb_data = input->button_data[INPUT_BUTTON_MOUSE_LEFT];
			if (!(lmb_data.flags & INPUT_BUTTON_FLAG_ENDED_DOWN) && lmb_data.num_transitions
			    && sprite_id) {
				Entity *mover = game_get_entity_by_id(&program->game,
				                                      current_choice.entity_id);
				Entity *target = game_get_entity_by_id(&program->game, sprite_id);
				Pos start = mover->pos;
				Pos end = target->pos;
				Action action = {};
				action.type = ACTION_MOVE;
				action.move.entity_id = current_choice.entity_id;
				action.move.start = start;
				action.move.end = end;
				u8 did_action = game_do_action(&program->game, action, &event_buffer);
				if (did_action) {
					game_play_until_input_required(&program->game, &event_buffer);
					world_anim_build_events_to_be_animated(&program->world_anim,
					                                       &event_buffer);
				}
			}
			break;
		}
		}
	}


	// do stuff
	imgui_begin(&program->imgui);
	imgui_set_text_cursor(&program->imgui, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
	char buffer[1024];
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse Pos: (%u, %u)",
	         input->mouse_pos.x, input->mouse_pos.y);
	imgui_text(&program->imgui, buffer);
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse Delta: (%d, %d)",
	         input->mouse_delta.x, input->mouse_delta.y);
	imgui_text(&program->imgui, buffer);
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse world pos: (%d, %d)",
	         world_mouse_pos.x, world_mouse_pos.y);
	imgui_text(&program->imgui, buffer);
	sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);
	snprintf(buffer, ARRAY_SIZE(buffer), "Sprite ID: %u", sprite_id);
	imgui_text(&program->imgui, buffer);
}

void render_d3d11(Program* program, ID3D11DeviceContext* dc, ID3D11RenderTargetView* output_rtv)
{
	v2_u32 screen_size_u32 = { (u32)program->screen_size.x, (u32)program->screen_size.y };

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(program->d3d11.output_rtv, clear_color);

	sprite_sheet_renderer_d3d11_begin(&program->draw.renderer, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.tiles, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.creatures, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.water_edges, dc, screen_size_u32);
	sprite_sheet_renderer_d3d11_end(&program->draw.renderer, dc);

	f32 zoom = program->draw.camera.zoom;
	/* v2_u32 input_size = { (u32)(((f32)screen_size_u32.w) / zoom),
	                      (u32)(((f32)screen_size_u32.h) / zoom) }; */
	v2_i32 world_tl = screen_pos_to_world_pos(&program->draw.camera,
	                                          screen_size_u32,
	                                          { 0, 0 });
	v2_i32 world_br = screen_pos_to_world_pos(&program->draw.camera,
	                                          screen_size_u32,
	                                          screen_size_u32);
	v2_u32 input_size = { (u32)(world_br.x - world_tl.x), (u32)(world_br.y - world_tl.y) };
	pixel_art_upsampler_d3d11_draw(&program->pixel_art_upsampler,
	                               dc,
	                               program->draw.renderer.d3d11.output_srv,
	                               program->d3d11.output_uav,
	                               input_size,
	                               world_tl,
	                               screen_size_u32,
	                               { 0, 0 });

	imgui_d3d11_draw(&program->imgui, dc, program->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(program->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(program->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &program->d3d11.output_srv);
	dc->Draw(6, 0);
}
