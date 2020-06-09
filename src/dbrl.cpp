#include "dbrl.h"

#include "jfg/imgui.h"
#include "jfg/jfg_math.h"
#include "jfg/log.h"
#include "jfg/debug_line_draw.h"
#include "jfg/containers.hpp"
#include "jfg/random.h"
#include "jfg/thread.h"

#include "sound.h"
#include "sprite_sheet.h"
#include "card_render.h"
#include "pixel_art_upsampler.h"

#include <stdio.h>  // XXX - for snprintf

#include "gen/appearance.data.h"
#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"
#include "gen/sprite_sheet_water_edges.data.h"

#include "gen/pass_through_dxbc_vertex_shader.data.h"
#include "gen/pass_through_dxbc_pixel_shader.data.h"

// =============================================================================
// Global Platform Functions

#define PLATFORM_FUNCTION(return_type, name, ...) return_type (*name)(__VA_ARGS__) = NULL;
PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION

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

// =============================================================================
// cards

struct Card
{
	u32             id;
	Card_Appearance appearance;
};

enum Card_Event_Type
{
	CARD_EVENT_DRAW,
};

struct Card_Event
{
	Card_Event_Type type;
	union {
		struct {
			u32             card_id;
			Card_Appearance appearance;
		} draw;
	};
};

#define CARD_STATE_MAX_CARDS  1024
#define CARD_STATE_MAX_EVENTS 1024
struct Card_State
{
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        deck;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        discard;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        hand;
	Max_Length_Array<Card_Event, CARD_STATE_MAX_EVENTS> events;

	void draw()
	{
		if (!deck) {
			while (discard) {
				u32 idx = rand_u32() % discard.len;
				deck.append(discard[idx]);
				discard[idx] = discard[discard.len - 1];
				--discard.len;
			}
		}
		ASSERT(deck);
		Card card = deck.pop();
		hand.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_DRAW;
		event.draw.card_id    = card.id;
		event.draw.appearance = card.appearance;
		events.append(event);
	}
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

	Card_State card_state;
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
					action.move.end = poss[rand_u32() % num_poss];
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
// turns

// TODO -- need a "peek move resolve" function

void peek_move_resolve(Slice<Action> moves)
{
	for (u32 i = 0; i < moves.len; ++i) {
		Action a = moves[i];
		ASSERT(a.type == ACTION_MOVE);
		/*
		if (will_resolve(a)) {
			a.move.will_resolve = 1;
		} else {
			a.move.will_resolve = 0;
		}
		*/
	}
}

enum Message_Type
{
	MESSAGE_MOVE_PRE_EXIT,
	MESSAGE_MOVE_POST_EXIT,
	MESSAGE_MOVE_PRE_ENTER,
	MESSAGE_MOVE_POST_ENTER,
};

struct Message
{
	Message_Type type;
	union {
		struct {
			Entity_ID entity_id;
			Pos       start;
			Pos       pos;
		} move;
	};
};

enum Transaction_Type
{
	TRANSACTION_MOVE_EXIT,
	TRANSACTION_MOVE_ENTER,
};

struct Transaction
{
	Transaction_Type type;
	f32 start_time;
	union {
		struct {
		} move_exit;
		struct {
		} move_enter;
	};
};

#define MAX_TRANSACTIONS 4096
void game_do_actions(Slice<Action> actions, Event_Buffer* event_buffer)
{
	Max_Length_Array<Transaction, MAX_TRANSACTIONS> transaction_buffer;

	// build actions into "transaction buffer"
	for (u32 i = 0; i < actions.len; ++i) {
		Action a = actions[i];
		Transaction t;
		switch (a.type) {
		case ACTION_NONE:
		case ACTION_WAIT:
			break;
		case ACTION_MOVE:
			t.type = TRANSACTION_MOVE_EXIT;
			t.start_time = 0.0f;
			transaction_buffer.append(t);
			break;
		}
	}

	f32 time = 0.0f;
	while (transaction_buffer) {
	}
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
	Card_Render card_render;
	Debug_Line card_debug_line;
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
			ca.idle.duration = 0.8f + 0.4f * rand_f32();
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
				anim->idle.duration = 0.8f + 0.4f * rand_f32();
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
// card anim

struct Hand_Params
{
	// in
	f32 screen_width; // x in range [-screen_width, screen_width]
	f32 height;
	f32 border;
	f32 bottom;
	f32 top;
	f32 separation;
	f32 num_cards;
	f32 highlighted_zoom;
	v2 card_size;

	// out
	f32 radius;
	f32 theta;
	v2  center;
};

void hand_params_calc(Hand_Params* params)
{
	f32 h = params->top - params->bottom;
	f32 target = ((params->num_cards - 1.0f) * params->separation) / h;

	f32 max_width = params->screen_width - params->border;

	f32 theta_low = 0;
	f32 theta_high = PI / 2.0f;
	while (theta_high - theta_low > 1e-6f) {
		f32 theta_mid = (theta_high + theta_low) / 2.0f;
		f32 val = theta_mid / (1.0f - cosf(theta_mid / 2.0f));
		if (val < target) {
			theta_high = theta_mid;
		} else {
			theta_low = theta_mid;
		}
	}
	f32 theta = (theta_low + theta_high) / 2.0f;
	f32 radius = ((params->num_cards - 1.0f) * params->separation) / theta;

	f32 card_diagonal, psi;
	{
		f32 w = params->card_size.w, h = params->card_size.h;
		card_diagonal = sqrtf(w*w + h*h);

		psi = atanf(w / h);
	}

	if (radius * sinf(theta / 2.0f) + card_diagonal * sinf(theta / 2.0f + psi) > max_width) {
		target = max_width / h;
		theta_low = 0;
		theta_high = PI / 2.0f;
		while (theta_high - theta_low > 1e-6f) {
			f32 theta_mid = (theta_high + theta_low) / 2.0f;
			f32 val = sinf(theta_mid / 2.0f) / (1.0f - cosf(theta_mid / 2.0f))
			        + (card_diagonal / h) * sinf(theta_mid / 2.0f + psi);
			f32 val_2 = sinf(theta_mid / 2.0f) / (1.0f - cosf(theta_mid / 2.0f));
			if (val < target) {
				theta_high = theta_mid;
			} else {
				theta_low = theta_mid;
			}
		}
		theta = (theta_high + theta_low) / 2.0f;
		// radius = max_width / sinf(theta / 2.0f);
		radius = h / (1 - cosf(theta / 2.0f));
		params->separation = radius * theta / (params->num_cards - 1.0f);
	}

	v2 center = {};
	center.x = 0.0f;
	center.y = params->top - radius;

	params->radius = radius;
	params->theta  = theta;
	params->center = center;
}

void hand_calc_deltas(f32* deltas, Hand_Params* params, u32 selected_card)
{
	deltas[selected_card] = 0.0f;

	f32 min_left_separation = (params->highlighted_zoom - 0.4f) * params->card_size.w;
	f32 left_delta = max(min_left_separation - params->separation, 0.0f);
	f32 left_ratio = max(1.0f - 0.5f * params->separation / left_delta, 0.4f);

	f32 acc = left_delta / params->radius;
	for (u32 i = selected_card; i; --i) {
		deltas[i - 1] = acc;
		acc *= left_ratio;
	}

	f32 min_right_separation = (0.7f + params->highlighted_zoom) * params->card_size.w;
	f32 right_delta = max(min_right_separation - params->separation, 0.0f);
	f32 right_ratio = 1.0f - 0.5f * params->separation / right_delta;

	acc = right_delta / params->radius;
	for (u32 i = selected_card + 1; i < params->num_cards; ++i) {
		deltas[i] = -acc;
		acc *= right_ratio;
	}
}

enum Card_Anim_State_Type
{
	CARD_ANIM_STATE_DRAWING,
	CARD_ANIM_STATE_SELECT,
};

enum Card_Anim_Type
{
	CARD_ANIM_DECK,
	CARD_ANIM_DISCARD,
	CARD_ANIM_DRAW,
	CARD_ANIM_IN_HAND,
	CARD_ANIM_HAND_TO_HAND,
};

struct Card_Hand_Pos
{
	f32 angle;
	f32 zoom;
	f32 radius;
	f32 angle_speed;
	f32 zoom_speed;
	f32 radius_speed;
};

struct Card_Anim
{
	Card_Anim_Type type;
	union {
		struct {
			u32 index;
		} hand;
		struct {
			f32 start_time;
			f32 duration;
			Card_Hand_Pos start;
			Card_Hand_Pos end;
			u32 index;
		} hand_to_hand;
		struct {
			f32 start_time;
			f32 duration;
			u32 hand_index;
		} draw;
	};
	u32 card_id;
	v2 card_face;
};

#define MAX_CARD_ANIMS 1024
struct Card_Anim_State
{
	Card_Anim_State_Type state;
	Hand_Params          hand_params;
	u32                  hand_size;
	u32                  highlighted_card_id;
	u32                  num_card_anims;
	Card_Anim            card_anims[MAX_CARD_ANIMS];
};

enum Card_UI_Event_Type
{
	CARD_UI_EVENT_NONE,
	CARD_UI_EVENT_DECK_CLICKED,
	CARD_UI_EVENT_DISCARD_CLICKED,
	CARD_UI_EVENT_HAND_CLICKED,
};

struct Card_UI_Event
{
	Card_UI_Event_Type type;
	union {
		struct {
			u32 card_id;
		} hand;
	};
};

void card_anim_update_anims(Card_Anim_State*  card_anim_state,
                            Slice<Card_Event> events,
                            f32               time)
{
	for (u32 i = 0; i < events.len; ++i) {
		Card_Event *event = &events[i];
		switch (event->type) {
		case CARD_EVENT_DRAW: {
			u32 hand_index = card_anim_state->hand_size++;
			Card_Anim anim = {};
			anim.type = CARD_ANIM_DRAW;
			anim.draw.start_time = time;
			anim.draw.duration = 1.0f;
			anim.draw.hand_index = hand_index;
			anim.card_id = event->draw.card_id;
			anim.card_face = card_appearance_get_sprite_coords(event->draw.appearance);
			card_anim_state->card_anims[card_anim_state->num_card_anims++] = anim;
			break;
		}
		}
	}
}

Card_UI_Event card_anim_draw(Card_Anim_State* card_anim_state,
                             Card_Render*     card_render,
                             v2_u32           screen_size,
                             f32              time,
                             Input*           input)
{
	const u32 discard_id = (u32)-1;
	const u32 deck_id    = (u32)-2;

	Card_UI_Event event = {};
	event.type = CARD_UI_EVENT_NONE;

	// TODO -- get card id from mouse pos
	v2 card_mouse_pos = { (f32)input->mouse_pos.x / (f32)screen_size.x,
	                      (f32)input->mouse_pos.y / (f32)screen_size.y };
	card_mouse_pos.x = (card_mouse_pos.x * 2.0f - 1.0f) * ((f32)screen_size.x / (f32)screen_size.y);
	card_mouse_pos.y = 1.0f - 2.0f * card_mouse_pos.y;
	u32 highlighted_card_id = card_render_get_card_id_from_mouse_pos(card_render, card_mouse_pos);

	if (highlighted_card_id && input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
		if (highlighted_card_id == discard_id) {
			event.type = CARD_UI_EVENT_DISCARD_CLICKED;
		} else if (highlighted_card_id == deck_id) {
			event.type = CARD_UI_EVENT_DECK_CLICKED;
		} else {
			event.type = CARD_UI_EVENT_HAND_CLICKED;
			event.hand.card_id = highlighted_card_id;
		}
	}

	card_render_reset(card_render);

	float ratio = (f32)screen_size.x / (f32)screen_size.y;

	u32 hand_size = card_anim_state->hand_size;
	Hand_Params *params = &card_anim_state->hand_params;
	params->screen_width = ratio;
	params->num_cards = (f32)hand_size;
	params->separation = params->card_size.w;
	params->highlighted_zoom = 1.2f;
	hand_params_calc(params);

	u32 num_card_anims = card_anim_state->num_card_anims;

	// convert finished card anims
	for (u32 i = 0; i < num_card_anims; ++i) {
		Card_Anim *anim = &card_anim_state->card_anims[i];
		switch (anim->type) {
		case CARD_ANIM_DECK:
			break;
		case CARD_ANIM_DISCARD:
			break;
		case CARD_ANIM_IN_HAND:
			break;
		case CARD_ANIM_DRAW:
			if (anim->draw.start_time + anim->draw.duration <= time) {
				anim->hand.index = anim->draw.hand_index;
				anim->type = CARD_ANIM_IN_HAND;
			}
			break;
		case CARD_ANIM_HAND_TO_HAND:
			if (anim->hand_to_hand.start_time + anim->hand_to_hand.duration <= time) {
				anim->hand.index = anim->hand_to_hand.index;
				anim->type = CARD_ANIM_IN_HAND;
			}
			break;
		}
	}

	// process hand to hand anims
	u32 prev_highlighted_card_id = card_anim_state->highlighted_card_id;
	u32 prev_highlighted_card = 0, highlighted_card = 0;

	// get highlighted card positions
	for (u32 i = 0; i < num_card_anims; ++i) {
		Card_Anim *anim = &card_anim_state->card_anims[i];
		u32 hand_index;
		switch (anim->type) {
		case CARD_ANIM_IN_HAND:
			hand_index = anim->hand.index;
			break;
		case CARD_ANIM_HAND_TO_HAND:
			hand_index = anim->hand_to_hand.index;
			break;
		default:
			continue;
		}
		if (anim->card_id == prev_highlighted_card_id) {
			prev_highlighted_card = hand_index;
		}
		if (anim->card_id == highlighted_card_id) {
			highlighted_card = hand_index;
		}
	}
	card_anim_state->highlighted_card_id = highlighted_card_id;

	u8 prev_hi_card_was_hand = prev_highlighted_card_id && prev_highlighted_card_id < (u32)-2;
	u8 hi_card_is_hand = highlighted_card_id && highlighted_card_id < (u32)-2;
	if (prev_highlighted_card_id != highlighted_card_id
	 && (prev_hi_card_was_hand || hi_card_is_hand)) {
		f32 before_deltas[100];
		f32 after_deltas[100];

		if (prev_highlighted_card_id) {
			hand_calc_deltas(before_deltas, params, prev_highlighted_card);
		} else {
			memset(before_deltas, 0, (u32)params->num_cards * sizeof(before_deltas[0]));
		}
		if (highlighted_card_id) {
			hand_calc_deltas(after_deltas, params, highlighted_card);
		} else {
			memset(after_deltas, 0, (u32)params->num_cards * sizeof(after_deltas[0]));
		}

		f32 base_delta = 1.0f / (params->num_cards - 1.0f);

		for (u32 i = 0; i < num_card_anims; ++i) {
			Card_Anim *anim = &card_anim_state->card_anims[i];
			Card_Hand_Pos before_pos = {};
			u32 hand_index;
			switch (anim->type) {
			case CARD_ANIM_IN_HAND: {
				hand_index = anim->hand.index;
				f32 base_angle = PI/2.0f + params->theta
				               * (0.5f - (f32)hand_index * base_delta);
				before_pos.angle = base_angle + before_deltas[hand_index];
				before_pos.radius = params->radius;
				before_pos.zoom = i == prev_highlighted_card && prev_highlighted_card_id
				                  ? params->highlighted_zoom : 1.0f;
				break;
			}
			case CARD_ANIM_HAND_TO_HAND: {
				hand_index = anim->hand_to_hand.index;
				Card_Hand_Pos *s = &anim->hand_to_hand.start;
				Card_Hand_Pos *e = &anim->hand_to_hand.end;
				f32 dt = (time - anim->hand_to_hand.start_time)
				         / anim->hand_to_hand.duration;
				dt = clamp(0.0f, 1.0f, dt);
				before_pos.angle  = lerp(s->angle,  e->angle,  dt);
				before_pos.zoom   = lerp(s->zoom,   e->zoom,   dt);
				before_pos.radius = lerp(s->radius, e->radius, dt);
				break;
			}
			default:
				continue;
			}
			f32 base_angle = PI/2.0f + params->theta * (0.5f - (f32)hand_index * base_delta);
			Card_Hand_Pos after_pos = {};
			after_pos.angle = base_angle + after_deltas[hand_index];
			after_pos.zoom = i == highlighted_card && highlighted_card_id
			               ? params->highlighted_zoom : 1.0f;
			after_pos.radius = params->radius;
			anim->type = CARD_ANIM_HAND_TO_HAND;
			anim->hand_to_hand.start_time = time;
			anim->hand_to_hand.duration = 0.1f;
			anim->hand_to_hand.start = before_pos;
			anim->hand_to_hand.end = after_pos;
			anim->hand_to_hand.index = hand_index;
		}
	}

	u8 is_a_card_highlighted = card_anim_state->highlighted_card_id
	                        && card_anim_state->highlighted_card_id < (u32)-2;
	f32 deltas[100];
	if (is_a_card_highlighted) {
		hand_calc_deltas(deltas, params, highlighted_card);
	} else {
		memset(deltas, 0, (u32)params->num_cards * sizeof(deltas[0]));
	}

	for (u32 i = 0; i < num_card_anims; ++i) {
		Card_Anim *anim = &card_anim_state->card_anims[i];
		switch (anim->type) {
		case CARD_ANIM_DECK:
			break;
		case CARD_ANIM_DISCARD:
			break;
		case CARD_ANIM_IN_HAND: {
			u32 i = anim->hand.index;
			f32 dist_to_highlighted = (f32)i - (f32)highlighted_card;

			v2 card_pos = {};
			f32 angle = PI / 2.0f + params->theta * (0.5f - ((f32)i / (f32)(hand_size - 1)));
			angle += deltas[i];
			f32 r = params->radius;
			if (is_a_card_highlighted && highlighted_card == i) {
				r += (params->highlighted_zoom - 1.0f) * params->card_size.h;
			}
			card_pos.x = r * cosf(angle) + params->center.x;
			card_pos.y = r * sinf(angle) + params->center.y;

			Card_Render_Instance instance = {};
			instance.screen_rotation = angle - PI / 2.0f;
			instance.screen_pos = card_pos;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = i;
			instance.zoom = 1.0f;
			if (is_a_card_highlighted && highlighted_card == i) {
				instance.zoom = params->highlighted_zoom;
			}
			card_render_add_instance(card_render, instance);
			break;
		}
		case CARD_ANIM_DRAW: {
			u32 i = anim->draw.hand_index;

			v2 start_pos = { -ratio + params->border / 2.0f,
			                 -1.0f  + params->height / 2.0f };
			f32 start_rotation = 0.0f;

			v2 end_pos = {};
			f32 angle = PI / 2.0f + params->theta * (0.5f - ((f32)i / (f32)(hand_size - 1)));
			end_pos.x = params->radius * cosf(angle) + params->center.x;
			end_pos.y = params->radius * sinf(angle) + params->center.y;
			f32 end_rotation = angle - PI / 2.0f;

			f32 dt = (time - anim->draw.start_time) / anim->draw.duration;
			if (dt < 0.0f) {
				dt = 0.0f;
			}
			f32 smooth = (3.0f - 2.0f * dt) * dt * dt;
			f32 jump   = dt * (1.0f - dt) * 4.0f;

			Card_Render_Instance instance = {};
			instance.screen_rotation = start_rotation
			                         + smooth * (end_rotation - start_rotation);
			instance.screen_pos.x = start_pos.x + smooth * (end_pos.x - start_pos.x);
			instance.screen_pos.y = start_pos.y + smooth * (end_pos.y - start_pos.y);
			instance.screen_pos.y += 0.1f * jump;
			instance.horizontal_rotation = (1.0f - dt) * PI;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = dt < 0.5f ? 2*num_card_anims - i : i;
			instance.zoom = 1.0f;
			card_render_add_instance(card_render, instance);
			break;
		}
		case CARD_ANIM_HAND_TO_HAND: {
			u32 i = anim->hand_to_hand.index;
			f32 dt = (time - anim->hand_to_hand.start_time) / anim->hand_to_hand.duration;

			Card_Hand_Pos *s = &anim->hand_to_hand.start;
			Card_Hand_Pos *e = &anim->hand_to_hand.end;

			f32 a = lerp(s->angle,  e->angle,  dt);
			f32 r = lerp(s->radius, e->radius, dt);
			f32 z = lerp(s->zoom,   e->zoom,   dt);
			r += (z - 1.0f) * params->card_size.h;

			v2 pos = {};
			pos.x = r * cosf(a) + params->center.x;
			pos.y = r * sinf(a) + params->center.y;

			Card_Render_Instance instance = {};
			instance.screen_rotation = a - PI / 2.0f;
			instance.screen_pos = pos;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = i;
			instance.zoom = z;
			card_render_add_instance(card_render, instance);

			break;
		}
		}
	}

	// add draw pile card
	{
		Card_Render_Instance instance = {};
		instance.screen_rotation = 0.0f;
		instance.screen_pos = { -ratio + params->border / 2.0f,
		                        -1.0f  + params->height / 2.0f };
		instance.card_pos = { 0.0f, 0.0f };
		instance.zoom = 1.0f;
		instance.card_id = deck_id;
		card_render_add_instance(card_render, instance);
	}

	// add discard pile card
	{
		Card_Render_Instance instance = {};
		instance.screen_rotation = 0.0f;
		instance.screen_pos = { ratio - params->border / 2.0f,
		                        -1.0f + params->height / 2.0f };
		instance.card_pos = { 1.0f, 0.0f };
		instance.zoom = 1.0f;
		instance.card_id = discard_id;
		card_render_add_instance(card_render, instance);
	}

	card_render_z_sort(card_render);

	return event;
}

// =============================================================================
// program

enum Program_State
{
	PROGRAM_STATE_NORMAL,
	PROGRAM_STATE_DEBUG_PAUSE,
};

enum Program_Input_State
{
	GIS_NONE,
	GIS_DRAGGING_MAP,
};

struct Program
{
	Program_State       state;
	volatile u32        process_frame_signal;

	Input  *cur_input;
	v2_u32  cur_screen_size;

	Game                game;
	Program_Input_State program_input_state;
	World_Anim_State    world_anim;
	Draw                draw;
	Sound_Player        sound;

	u32           frame_number;
	v2            screen_size;
	v2_u32        max_screen_size;
	IMGUI_Context imgui;

	MT19937 random_state;

	u32 prev_card_id;

	Card_Anim_State card_anim_state;

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

void program_init(Program* program, Platform_Functions platform_functions)
{
#define PLATFORM_FUNCTION(_return_type, name, ...) name = platform_functions.name;
	PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION

	memset(program, 0, sizeof(*program));
	sprite_sheet_renderer_init(&program->draw.renderer,
	                           &program->draw.tiles, 3,
	                           { 1600, 900 });

	program->random_state.seed(0);
	program->random_state.set_current();

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

	u32 deck_size = 100;
	Card_State *card_state = &program->game.card_state;
	for (u32 i = 0; i < deck_size; ++i) {
		Card card = {};
		card.id = i + 1;
		card.appearance = (Card_Appearance)(rand_u32() % NUM_CARD_APPEARANCES);
		card_state->discard.append(card);
	}
}

u8 program_dsound_init(Program* program, IDirectSound* dsound)
{
	return sound_player_dsound_init(&program->sound, dsound);
}

void program_dsound_free(Program* program)
{
	sound_player_dsound_free(&program->sound);
}

void program_dsound_play(Program* program)
{
	sound_player_dsound_play(&program->sound);
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

	if (!card_render_d3d11_init(&program->draw.card_render, device)) {
		goto error_init_card_render;
	}

	if (!debug_line_d3d11_init(&program->draw.card_debug_line, device)) {
		goto error_init_debug_line;
	}

	program->max_screen_size      = screen_size;
	program->d3d11.output_texture = output_texture;
	program->d3d11.output_uav     = output_uav;
	program->d3d11.output_rtv     = output_rtv;
	program->d3d11.output_srv     = output_srv;
	program->d3d11.output_vs      = output_vs;
	program->d3d11.output_ps      = output_ps;

	return 1;

	debug_line_d3d11_free(&program->draw.card_debug_line);
error_init_debug_line:
	card_render_d3d11_free(&program->draw.card_render);
error_init_card_render:
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
	debug_line_d3d11_free(&program->draw.card_debug_line);
	card_render_d3d11_free(&program->draw.card_render);
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

void process_frame_aux(Program* program, Input* input, v2_u32 screen_size)
{
	// start off by setting global state
	program->random_state.set_current();

	program->screen_size = { (f32)screen_size.w, (f32)screen_size.h };
	++program->frame_number;
	program->sound.reset();

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

	// program->card_anim_state.hand_params.screen_width = ratio;
	program->card_anim_state.hand_params.height = 0.5f;
	program->card_anim_state.hand_params.border = 0.4;
	program->card_anim_state.hand_params.top = -0.7f;
	program->card_anim_state.hand_params.bottom = -0.9f;
	// XXX - ugh
	program->card_anim_state.hand_params.card_size = { 0.5f*0.4f*48.0f/80.0f, 0.5f*0.4f*1.0f };

	debug_line_reset(&program->draw.card_debug_line);
	// card_anim_update_anims
	Card_UI_Event card_event = card_anim_draw(&program->card_anim_state,
	                                          &program->draw.card_render,
	                                          screen_size,
	                                          time,
	                                          input);

	Card_State *card_state = &program->game.card_state;
	card_state->events.reset();
	switch (card_event.type) {
	case CARD_UI_EVENT_NONE:
		break;
	case CARD_UI_EVENT_DECK_CLICKED:
		card_state->draw();
		program->sound.play(SOUND_DEAL_CARD);
		break;
	case CARD_UI_EVENT_DISCARD_CLICKED:
		break;
	case CARD_UI_EVENT_HAND_CLICKED:
		break;
	}

	card_anim_update_anims(&program->card_anim_state, card_state->events, time);

	// TODO -- get card id from mouse pos
	v2 card_mouse_pos = { (f32)input->mouse_pos.x / (f32)screen_size.x,
	                      (f32)input->mouse_pos.y / (f32)screen_size.y };
	card_mouse_pos.x = (card_mouse_pos.x * 2.0f - 1.0f) * ((f32)screen_size.x / (f32)screen_size.y);
	card_mouse_pos.y = 1.0f - 2.0f * card_mouse_pos.y;
	u32 selected_card_id = card_render_get_card_id_from_mouse_pos(&program->draw.card_render,
	                                                              card_mouse_pos);
	program->prev_card_id = selected_card_id;
	{
		f32 ratio = (f32)screen_size.x / (f32) screen_size.y;
		program->draw.card_debug_line.constants.top_left     = { -ratio,  1.0f };
		program->draw.card_debug_line.constants.bottom_right = {  ratio, -1.0f };
	}
	card_render_draw_debug_lines(&program->draw.card_render,
	                             &program->draw.card_debug_line,
	                             selected_card_id);


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
	snprintf(buffer, ARRAY_SIZE(buffer), "Card mouse pos: (%f, %f)",
	         card_mouse_pos.x, card_mouse_pos.y);
	imgui_text(&program->imgui, buffer);
	snprintf(buffer, ARRAY_SIZE(buffer), "Selected Card ID: %u", selected_card_id);
	imgui_text(&program->imgui, buffer);
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
	process_frame_aux(program, program->cur_input, program->cur_screen_size);
	ASSERT(!interlocked_compare_exchange(&program->process_frame_signal, 1, 0));
}

void process_frame(Program* program, Input* input, v2_u32 screen_size)
{
	switch (program->state) {
	case PROGRAM_STATE_NORMAL: {
		program->cur_input       = input;
		program->cur_screen_size = screen_size;
		start_thread(process_frame_aux_thread, program);
		while (!interlocked_compare_exchange(&program->process_frame_signal, 0, 1)) {
			sleep(0);
		}
		break;
	}
	case PROGRAM_STATE_DEBUG_PAUSE:
		break;
	}
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

	card_render_d3d11_draw(&program->draw.card_render,
	                       dc,
	                       screen_size_u32,
	                       program->d3d11.output_rtv);
	// debug_line_d3d11_draw(&program->draw.card_debug_line, dc, program->d3d11.output_rtv);

	imgui_d3d11_draw(&program->imgui, dc, program->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(program->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(program->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &program->d3d11.output_srv);
	dc->Draw(6, 0);
}
