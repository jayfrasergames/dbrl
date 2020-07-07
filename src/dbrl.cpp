#include "dbrl.h"

#include "jfg/imgui.h"
#include "jfg/jfg_math.h"
#include "jfg/log.h"
#include "jfg/debug_line_draw.h"
#include "jfg/containers.hpp"
#include "jfg/random.h"
#include "jfg/thread.h"

#include "assets.h"
#include "constants.h"
#include "debug_draw_world.h"
#include "sound.h"
#include "sprite_sheet.h"
#include "card_render.h"
#include "pixel_art_upsampler.h"

#include <stdio.h>  // XXX - for snprintf

#include "gen/appearance.data.h"
#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"
#include "gen/sprite_sheet_water_edges.data.h"
#include "gen/sprite_sheet_effects_32.data.h"
#include "gen/boxy_bold.data.h"

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

u16 pos_to_u16(Pos p)
{
	return p.y * 256 + p.x;
}

Pos u16_to_pos(u16 n)
{
	return V2_u8(n % 256, n / 256);
}

#define MAX_ENTITIES 10240

typedef u16 Entity_ID;

struct Program;
thread_local Program* global_program;
thread_local Log* debug_log;
void debug_pause();

template <typename T>
struct Map_Cache
{
	T items[256 * 256];
	T& operator[](Pos pos) { return items[pos.y * 256 + pos.x]; }
};

struct Map_Cache_Bool
{
	u64  items[256 * 256 / 64];
	u64  get(Pos p)   { u16 idx = pos_to_u16(p); return items[idx / 64] & ((u64)1 << (idx % 64)); }
	void set(Pos p)   { u16 idx = pos_to_u16(p); items[idx / 64] |= ((u64)1 << (idx % 64)); }
	void unset(Pos p) { u16 idx = pos_to_u16(p); items[idx / 64] &= ~((u64)1 << (idx % 64)); }
	void reset()      { memset(items, 0, sizeof(items)); }
};

// =============================================================================
// game

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
	ACTION_FIREBALL,
	ACTION_EXCHANGE,
	ACTION_BLINK,
	ACTION_POISON,
};

struct Action
{
	Action_Type type;
	union {
		struct {
			Entity_ID entity_id;
			Pos start, end;
		} move;
		struct {
			Pos start;
			Pos end;
		} fireball;
		struct {
			// Entity_ID caster;
			Entity_ID a;
			Entity_ID b;
		} exchange;
		struct {
			Entity_ID caster;
			Pos       target;
		} blink;
		struct {
			Entity_ID target;
		} poison;
	};

	operator bool() { return type; }
};

#define GAME_MAX_ACTIONS 1024
typedef Max_Length_Array<Action, GAME_MAX_ACTIONS> Action_Buffer;

// Transactions

enum Event_Type
{
	EVENT_NONE,
	EVENT_MOVE,
	EVENT_MOVE_BLOCKED,
	EVENT_DROP_TILE,
	EVENT_FIREBALL_HIT,
	EVENT_FIREBALL_SHOT,
	EVENT_FIREBALL_OFFSHOOT,
	EVENT_STUCK,
	EVENT_DAMAGED,
	EVENT_DEATH,
	EVENT_EXCHANGE,
	EVENT_BLINK,
	EVENT_SLIME_SPLIT,
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
		struct {
			Entity_ID entity_id;
			Pos pos;
		} stuck;
		struct {
			Pos pos;
		} drop_tile;
		struct {
			f32 duration;
			Pos start;
			Pos end;
		} fireball_shot;
		struct {
			Pos pos;
		} fireball_hit;
		struct {
			f32 duration;
			Pos start;
			Pos end;
		} fireball_offshoot;
		struct {
			Entity_ID entity_id;
			Pos pos;
			u32 amount;
		} damaged;
		struct {
			Entity_ID entity_id;
		} death;
		struct {
			Entity_ID a, b;
		} exchange;
		struct {
			Entity_ID caster_id;
			Pos       target;
		} blink;
		struct {
			Entity_ID original_id;
			Entity_ID new_id;
			v2 start;
			v2 end;
		} slime_split;
	};
};

#define MAX_EVENTS 10240
typedef Max_Length_Array<Event, MAX_EVENTS> Event_Buffer;

u8 event_type_is_blocking(Event_Type type)
{
	switch (type) {
	case EVENT_NONE: return 0;
	case EVENT_MOVE: return 0;
	case EVENT_MOVE_BLOCKED: return 0;
	case EVENT_DROP_TILE: return 0;
	case EVENT_FIREBALL_HIT: return 0;
	case EVENT_FIREBALL_SHOT: return 0;
	case EVENT_FIREBALL_OFFSHOOT: return 0;
	case EVENT_STUCK: return 0;
	case EVENT_DAMAGED: return 0;
	case EVENT_DEATH: return 0;
	case EVENT_EXCHANGE: return 0;
	case EVENT_BLINK: return 0;
	case EVENT_SLIME_SPLIT: return 0;
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
	Entity_ID     id;
	u16           movement_type;
	u16           block_mask;
	Pos           pos;
	Appearance    appearance;
	i32           hit_points;
	i32           max_hit_points;
};

enum Controller_Type
{
	CONTROLLER_PLAYER,
	CONTROLLER_RANDOM_MOVE,
	CONTROLLER_RANDOM_SNAKE,
	CONTROLLER_DRAGON,
	CONTROLLER_SLIME,

	NUM_CONTROLLERS,
};

#define CONTROLLER_SNAKE_MAX_LENGTH 16

typedef u32 Controller_ID;
struct Controller
{
	Controller_Type type;
	Controller_ID   id;
	union {
		struct {
			Entity_ID entity_id;
			Action    action;
		} player;
		struct {
			Entity_ID entity_id;
		} random_move;
		struct {
			Max_Length_Array<Entity_ID, CONTROLLER_SNAKE_MAX_LENGTH> entities;
		} random_snake;
		struct {
			Entity_ID entity_id;
		} dragon;
		struct {
			Entity_ID entity_id;
			u32       split_cooldown;
		} slime;
	};
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
	CARD_EVENT_HAND_TO_IN_PLAY,
	CARD_EVENT_HAND_TO_DISCARD,
	CARD_EVENT_IN_PLAY_TO_DISCARD,
};

struct Card_Event
{
	Card_Event_Type type;
	union {
		struct {
			u32             card_id;
			Card_Appearance appearance;
		} draw;
		struct {
			u32 card_id;
		} hand_to_in_play;
		struct {
			u32 card_id;
		} hand_to_discard;
		struct {
			u32 card_id;
		} in_play_to_discard;
	};
};

#define CARD_STATE_MAX_CARDS  1024
#define CARD_STATE_MAX_EVENTS 1024
struct Card_State
{
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        deck;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        discard;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        hand;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        in_play;
	Max_Length_Array<Card_Event, CARD_STATE_MAX_EVENTS> events;
};

enum Tile_Type
{
	TILE_EMPTY,
	TILE_WALL,
	TILE_FLOOR,
	TILE_WATER,
};

struct Tile
{
	Tile_Type  type;
	Appearance appearance;
};

enum Message_Type : u32
{
	MESSAGE_MOVE_PRE_EXIT   = 1 << 0,
	MESSAGE_MOVE_POST_EXIT  = 1 << 1,
	MESSAGE_MOVE_PRE_ENTER  = 1 << 2,
	MESSAGE_MOVE_POST_ENTER = 1 << 3,
	MESSAGE_DAMAGE          = 1 << 4,
};

struct Message
{
	Message_Type type;
	union {
		struct {
			Entity_ID entity_id;
			Pos       start;
			Pos       end;
		} move;
		struct {
			Entity_ID entity_id;
			i32       amount;
			u8        entity_died;
		} damage;
	};
};

enum Message_Handler_Type
{
	MESSAGE_HANDLER_PREVENT_EXIT,
	MESSAGE_HANDLER_PREVENT_ENTER,
	MESSAGE_HANDLER_DROP_TILE,
	MESSAGE_HANDLER_TRAP_FIREBALL,
	MESSAGE_HANDLER_SLIME_SPLIT,
};

struct Message_Handler
{
	Message_Handler_Type type;
	u32                  handle_mask;
	Entity_ID            owner_id;
	union {
		struct {
			Pos pos;
		} prevent_exit;
		struct {
			Pos pos;
		} prevent_enter;
		struct {
			Pos pos;
		} trap;
	};
};

#define GAME_MAX_CONTROLLERS 1024
#define GAME_MAX_MESSAGE_HANDLERS 1024
// maybe better called "world state"?
struct Game
{
	u32 current_entity;
	Entity_ID     next_entity_id;
	Controller_ID next_controller_id;

	// probably don't need this
	u32 cur_block_id;
	f32 block_time;

	Max_Length_Array<Entity, MAX_ENTITIES> entities;

	Map_Cache<Tile> tiles;

	Max_Length_Array<Controller, GAME_MAX_CONTROLLERS> controllers;

	Card_State card_state;

	Max_Length_Array<Message_Handler, GAME_MAX_MESSAGE_HANDLERS> handlers;
};

Entity_ID game_add_slime(Game *game, Pos pos, u32 hit_points)
{
	Entity_ID entity_id = game->next_entity_id++;

	Entity e = {};
	e.hit_points = min(hit_points, 5);
	e.max_hit_points = 5;
	e.id = entity_id;
	e.pos = pos;
	e.appearance = APPEARANCE_CREATURE_GREEN_SLIME;
	e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
	e.movement_type = BLOCK_WALK;
	game->entities.append(e);

	Controller c = {};
	c.type = CONTROLLER_SLIME;
	c.slime.entity_id = entity_id;
	c.slime.split_cooldown = 5;
	game->controllers.append(c);

	Message_Handler mh = {};
	mh.type = MESSAGE_HANDLER_SLIME_SPLIT;
	mh.handle_mask = MESSAGE_DAMAGE;
	mh.owner_id = entity_id;
	game->handlers.append(mh);

	return entity_id;
}

void game_remove_entity(Game* game, Entity_ID entity_id)
{
	u32 num_entities = game->entities.len;
	Entity *e = game->entities.items;
	for (u32 i = 0; i < num_entities; ++i, ++e) {
		if (e->id == entity_id) {
			game->entities.remove(i);
			break;
		}
	}

	auto& controllers = game->controllers;
	for (u32 i = 0; i < controllers.len; ++i) {
		Controller *c = &controllers[i];
		switch (c->type) {
		case CONTROLLER_PLAYER:
			// TODO -- post player death event?
			if (c->player.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
		case CONTROLLER_RANDOM_MOVE:
			if (c->random_move.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
		case CONTROLLER_RANDOM_SNAKE:
			ASSERT(0);
			break;
		case CONTROLLER_DRAGON:
			if (c->dragon.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
		case CONTROLLER_SLIME:
			if (c->slime.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
		}
	}
break_loop: ;

	auto& handlers = game->handlers;
	for (u32 i = 0; i < handlers.len; ) {
		if (handlers[i].owner_id == entity_id) {
			handlers.remove(i);
			continue;
		}
		++i;
	}
}

Pos game_get_player_pos(Game* game)
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
	for (u32 i = 0; i < game->entities.len; ++i) {
		Entity *e = &game->entities[i];
		if (e->id == player_id) {
			return e->pos;
		}
	}
	ASSERT(0);
	return { 0, 0 };
}

Entity* game_get_entity_by_id(Game* game, Entity_ID entity_id)
{
	for (u32 i = 0; i < game->entities.len; ++i) {
		if (game->entities[i].id == entity_id) {
			return &game->entities[i];
		}
	}
	return NULL;
}

u8 tile_is_passable(Tile tile, u16 move_mask)
{
	switch (tile.type) {
	case TILE_EMPTY:
	case TILE_WALL:
		return 0;
	case TILE_FLOOR:
		if (move_mask & BLOCK_SWIM) {
			return 0;
		}
		return 1;
	case TILE_WATER:
		if (move_mask & BLOCK_WALK) {
			return 0;
		}
		return 1;
	}
	ASSERT(0);
	return 0;
}

u8 game_is_passable(Game* game, Pos pos, u16 move_mask)
{
	Tile t = game->tiles[pos];
	if (!tile_is_passable(t, move_mask)) {
		return 0;
	}
	u32 num_entities = game->entities.len;
	Entity *e = game->entities.items;
	u8 result = 1;
	for (u32 i = 0; i < num_entities; ++i, ++e) {
		if (e->pos.x == pos.x && e->pos.y == pos.y) {
			if (e->block_mask & move_mask) {
				return 0;
			}
		}
	}
	return result;
}

void game_draw_cards(Game* game, Action_Buffer* action_buffer, u32 num_to_draw)
{
	Card_State *card_state = &game->card_state;
	auto& actions = *action_buffer;
	actions.reset();
	if (!card_state->deck) {
		while (card_state->discard) {
			u32 idx = rand_u32() % card_state->discard.len;
			card_state->deck.append(card_state->discard[idx]);
			card_state->discard[idx] = card_state->discard[card_state->discard.len - 1];
			--card_state->discard.len;
		}
	}
	// ASSERT(card_state->deck);

	for (u32 i = 0; i < num_to_draw && card_state->deck; ++i) {
		Card card = card_state->deck.pop();
		card_state->hand.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_DRAW;
		event.draw.card_id    = card.id;
		event.draw.appearance = card.appearance;
		card_state->events.append(event);
		switch (card.appearance) {
		case CARD_APPEARANCE_POISON: {
			Action action = {};
			action.type = ACTION_POISON;
			Controller *c = &game->controllers[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			action.poison.target = c->player.entity_id;
			actions.append(action);
			break;
		}
		}
	}
}

void game_end_play_cards(Game* game)
{
	Card_State *card_state = &game->card_state;

	while (card_state->in_play) {
		Card card = card_state->in_play.pop();
		card_state->discard.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_IN_PLAY_TO_DISCARD;
		event.in_play_to_discard.card_id = card.id;
		card_state->events.append(event);
	}

	while (card_state->hand) {
		Card card = card_state->hand.pop();
		card_state->discard.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_HAND_TO_DISCARD;
		event.hand_to_discard.card_id = card.id;
		card_state->events.append(event);
	}
}


#define GAME_MAX_TRANSACTIONS 65536

enum Transaction_Type
{
	TRANSACTION_REMOVE,
	TRANSACTION_MOVE_EXIT,
	TRANSACTION_MOVE_PRE_ENTER,
	TRANSACTION_MOVE_ENTER,
	TRANSACTION_DROP_TILE,
	TRANSACTION_FIREBALL_SHOT,
	TRANSACTION_FIREBALL_OFFSHOOT,
	TRANSACTION_EXCHANGE_CAST,
	TRANSACTION_EXCHANGE,
	TRANSACTION_BLINK_CAST,
	TRANSACTION_BLINK,
	TRANSACTION_POISON_CAST,
	TRANSACTION_POISON,
	TRANSACTION_SLIME_SPLIT,
};

#define TRANSACTION_EPSILON 1e-6f;

struct Transaction
{
	Transaction_Type type;
	f32 start_time;
	union {
		struct {
			Entity_ID entity_id;
			Pos start;
			Pos end;
		} move;
		struct {
			Pos pos;
		} drop_tile;
		struct {
			Pos start;
			Pos end;
		} fireball_shot;
		struct {
			f32 start_time;
			Pos start;
			v2_i16 dir;
			u32 cur_step;
			u32 num_steps;
		} fireball_offshoot;
		struct {
			Entity_ID a;
			Entity_ID b;
		} exchange;
		struct {
			Entity_ID caster_id;
			Pos       target;
		} blink;
		struct {
			Entity_ID entity_id;
		} poison;
		struct {
			Entity_ID slime_id;
			u32       hit_points;
		} slime_split;
	};
};

typedef Max_Length_Array<Transaction, GAME_MAX_TRANSACTIONS> Transaction_Buffer;

void game_dispatch_message(Game*               game,
                           Message             message,
                           f32                 time,
                           Transaction_Buffer* transaction_buffer,
                           Event_Buffer*       event_buffer,
                           void*               data)
{
	auto& handlers = game->handlers;
	auto& transactions = *transaction_buffer;
	auto& events = *event_buffer;
	u32 num_handlers = handlers.len;
	for (u32 i = 0; i < num_handlers; ++i) {
		Message_Handler h = handlers[i];
		if (!(message.type & h.handle_mask)) {
			continue;
		}
		switch (h.type) {
		case MESSAGE_HANDLER_PREVENT_EXIT: {
			if (h.prevent_exit.pos == message.move.start) {
				u8 *can_exit = (u8*)data;
				*can_exit = 0;
			}
			break;
		}
		case MESSAGE_HANDLER_PREVENT_ENTER: {
			if (h.prevent_enter.pos.x == message.move.end.x
			 && h.prevent_enter.pos.y == message.move.end.y) {
				u8 *can_enter = (u8*)data;
				*can_enter = 0;
			}
			break;
		}
		case MESSAGE_HANDLER_DROP_TILE:
			if (h.trap.pos == message.move.start) {
				// game->tiles[h.trap.pos].type = TILE_EMPTY;
				Transaction t = {};
				t.type = TRANSACTION_DROP_TILE;
				t.start_time = time + 0.05f;
				t.drop_tile.pos = h.trap.pos;
				transactions.append(t);
			}
			break;
		case MESSAGE_HANDLER_TRAP_FIREBALL:
			if (h.trap.pos == message.move.end) {
				Transaction t = {};
				t.type = TRANSACTION_FIREBALL_OFFSHOOT;
				t.start_time = time;
				t.fireball_offshoot.start_time = time;
				t.fireball_offshoot.start = h.trap.pos;
				t.fireball_offshoot.cur_step = 0;
				t.fireball_offshoot.num_steps = 3;

				t.fireball_offshoot.dir = V2_i16( 0,  1);
				transactions.append(t);
				t.fireball_offshoot.dir = V2_i16( 0, -1);
				transactions.append(t);
				t.fireball_offshoot.dir = V2_i16( 1,  0);
				transactions.append(t);
				t.fireball_offshoot.dir = V2_i16(-1,  0);
				transactions.append(t);

				Event e = {};
				e.type = EVENT_FIREBALL_HIT;
				e.time = time;
				e.fireball_hit.pos = h.trap.pos;
				events.append(e);
			}
			break;
		case MESSAGE_HANDLER_SLIME_SPLIT:
			if (h.owner_id == message.damage.entity_id && !message.damage.entity_died) {
				Entity *e = game_get_entity_by_id(game, h.owner_id);
				Pos p = e->pos;
				/*
				debug_draw_world_reset();
				debug_draw_world_set_color(V4_f32(0.0f, 1.0f, 0.0f, 1.0f));
				debug_draw_world_circle((v2)p, 0.25f);
				debug_pause();
				*/
				Transaction t = {};
				t.type = TRANSACTION_SLIME_SPLIT;
				t.start_time = time + TRANSACTION_EPSILON;
				t.slime_split.slime_id = h.owner_id;
				t.slime_split.hit_points = e->hit_points;
				transactions.append(t);
			}
			break;
		}
	}
}

// #define DEBUG_CREATE_MOVES
// #define DEBUG_SHOW_ACTIONS
// #define DEBUG_TRANSACTION_PROCESSING

void game_simulate_actions(Game* game, Slice<Action> actions, Event_Buffer* event_buffer)
{
	u32 num_entities = game->entities.len;
	auto& tiles = game->tiles;

	// =====================================================================
	// simulate actions

	// 1. create transaction buffer


	Transaction_Buffer transactions;
	transactions.reset();

	for (u32 i = 0; i < actions.len; ++i) {
		Action action = actions[i];
		switch (action.type) {
		case ACTION_NONE:
		case ACTION_WAIT:
			break;
		case ACTION_MOVE: {
			Transaction t = {};
			t.type = TRANSACTION_MOVE_EXIT;
			t.start_time = 0.0f;
			t.move.entity_id = action.move.entity_id;
			t.move.start = action.move.start;
			t.move.end = action.move.end;
			transactions.append(t);
			break;
		}
		case ACTION_FIREBALL: {
			Transaction t = {};
			t.type = TRANSACTION_FIREBALL_SHOT;
			t.start_time = 0.0f;
			t.fireball_shot.start = action.fireball.start;
			t.fireball_shot.end = action.fireball.end;
			transactions.append(t);
			break;
		}
		case ACTION_EXCHANGE: {
			Transaction t = {};
			t.type = TRANSACTION_EXCHANGE_CAST;
			t.start_time = 0.0f;
			t.exchange.a = action.exchange.a;
			t.exchange.b = action.exchange.b;
			transactions.append(t);
			break;
		}
		case ACTION_BLINK: {
			Transaction t = {};
			t.type = TRANSACTION_BLINK_CAST;
			t.start_time = 0.0f;
			t.blink.caster_id = action.blink.caster;
			t.blink.target = action.blink.target;
			transactions.append(t);
			break;
		}
		case ACTION_POISON: {
			Transaction t = {};
			t.type = TRANSACTION_POISON_CAST;
			t.start_time = 0.0f;
			t.poison.entity_id = action.poison.target;
			transactions.append(t);
			break;
		}
		}
	}

	// 2. initialise "occupied" grid

	Map_Cache_Bool occupied;
	occupied.reset();

	Entity *entities = game->entities.items;
	for (u32 i = 0; i < num_entities; ++i) {
		Entity *e = &entities[i];
		if (e->block_mask) {
			occupied.set(e->pos);
		}
	}

	// 3. resolve transactions

	auto& handlers = game->handlers;
	Event_Buffer& events = *event_buffer;

	struct Entity_Damage
	{
		Entity_ID entity_id;
		i32       damage;
		Pos       pos;
	};
	Max_Length_Array<Entity_Damage, MAX_ENTITIES> entity_damage;

	f32 time = 0.0f;
	while (transactions) {
		entity_damage.reset();
		for (u32 i = 0; i < transactions.len; ++i) {
			Transaction *t = &transactions[i];
			ASSERT(t->start_time >= time);
			if (t->start_time > time) {
				continue;
			}
			switch (t->type) {
			case TRANSACTION_MOVE_EXIT: {
				// pre-exit
				u8 can_exit = 1;
				Message m = {};
				m.type = MESSAGE_MOVE_PRE_EXIT;
				m.move.entity_id = t->move.entity_id;
				m.move.start = t->move.start;
				m.move.end = t->move.end;
				game_dispatch_message(game, m, time, &transactions, event_buffer, &can_exit);

#ifdef DEBUG_TRANSACTION_PROCESSING
				debug_draw_world_reset();
				debug_draw_world_set_color(V4_f32(1.0f, 1.0f, 0.0f, 1.0f));
				debug_draw_world_arrow((v2)m.move.start, (v2)m.move.end);
				debug_pause();
#endif

				if (!can_exit) {
					Event event = {};
					event.type = EVENT_STUCK;
					event.time = time;
					event.stuck.entity_id = t->move.entity_id;
					event.stuck.pos = t->move.start;
					events.append(event);
					t->type = TRANSACTION_REMOVE;
					break;
				}

#ifdef DEBUG_TRANSACTION_PROCESSING
				debug_draw_world_reset();
				debug_draw_world_set_color(V4_f32(0.0f, 1.0f, 0.0f, 1.0f));
				debug_draw_world_arrow((v2)m.move.start, (v2)m.move.end);
				debug_pause();
#endif

				// post-exit
				m.type = MESSAGE_MOVE_POST_EXIT;
				game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);

				t->type = TRANSACTION_MOVE_PRE_ENTER;
				t->start_time += constants.anims.move.duration / 2.0f;
				break;
			}
			case TRANSACTION_MOVE_PRE_ENTER: {
				// XXX - check entity is still alive
				Entity *e = game_get_entity_by_id(game, t->move.entity_id);
				if (!e) {
					t->type = TRANSACTION_REMOVE;
					break;
				}

				// pre-enter
				u8 can_enter = 1;
				Pos start = t->move.start;
				Pos end = t->move.end;
				Message m = {};
				m.type = MESSAGE_MOVE_PRE_ENTER;
				m.move.entity_id = t->move.entity_id;
				m.move.start = start;
				m.move.end = end;
				game_dispatch_message(game, m, time, &transactions, event_buffer, &can_enter);

#ifdef DEBUG_TRANSACTION_PROCESSING
				debug_draw_world_reset();
				debug_draw_world_set_color(V4_f32(1.0f, 1.0f, 0.0f, 1.0f));
				debug_draw_world_arrow((v2)start, (v2)end);
				debug_pause();
#endif

				// post-enter
				Event event = {};
				event.time = time - (constants.anims.move.duration / 2.0f); // XXX - yuck
				event.move.entity_id = t->move.entity_id;
				event.move.start = start;
				event.move.end = end;
				if (can_enter && !occupied.get(end)) {
					occupied.unset(start);
					occupied.set(end);
					e->pos = end;
					event.type = EVENT_MOVE;

#ifdef DEBUG_TRANSACTION_PROCESSING
					debug_draw_world_reset();
					debug_draw_world_set_color(V4_f32(0.0f, 1.0f, 0.0f, 1.0f));
					debug_draw_world_arrow((v2)start, (v2)end);
					debug_pause();
#endif
				} else {
#ifdef DEBUG_TRANSACTION_PROCESSING
					debug_draw_world_reset();
					debug_draw_world_set_color(V4_f32(1.0f, 0.0f, 0.0f, 1.0f));
					debug_draw_world_circle((v2)start, 0.5f);
					debug_pause();
#endif
					t->move.end = t->move.start;
					event.type = EVENT_MOVE_BLOCKED;
				}
				events.append(event);

				t->type = TRANSACTION_MOVE_ENTER;
				t->start_time = time + constants.anims.move.duration / 2.0f;
				break;
			}
			case TRANSACTION_MOVE_ENTER: {
				// XXX - check entity is still alive
				Entity *e = game_get_entity_by_id(game, t->move.entity_id);
				if (!e) {
					t->type = TRANSACTION_REMOVE;
					break;
				}

				Pos start = t->move.start;
				Pos end = t->move.end;
				Message m = {};
				m.type = MESSAGE_MOVE_POST_ENTER;
				m.move.entity_id = t->move.entity_id;
				m.move.start = start;
				m.move.end = end;
				game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);
				t->type = TRANSACTION_REMOVE;
				break;
			}
			case TRANSACTION_DROP_TILE: {
				Event event = {};
				event.type = EVENT_DROP_TILE;
				event.time = time;
				event.drop_tile.pos = t->drop_tile.pos;
				events.append(event);

				tiles[t->drop_tile.pos].type = TILE_EMPTY;
				t->type = TRANSACTION_REMOVE;

				break;
			}
			case TRANSACTION_FIREBALL_SHOT: {
				// TODO -- calculate sensible start time/duration for fireball
				// based on distance the fireball has to travel
				Event event = {};
				event.type = EVENT_FIREBALL_SHOT;
				event.time = time;
				event.fireball_shot.duration = constants.anims.fireball.shot_duration;
				event.fireball_shot.start = t->fireball_shot.start;
				event.fireball_shot.end = t->fireball_shot.end;
				events.append(event);

				event.type = EVENT_FIREBALL_HIT;
				event.time = time + constants.anims.fireball.shot_duration;
				event.fireball_hit.pos = t->fireball_shot.end;
				events.append(event);

				t->type = TRANSACTION_REMOVE;

				f32 start_time = time + constants.anims.fireball.shot_duration;
				Transaction offshoot = {};
				offshoot.type = TRANSACTION_FIREBALL_OFFSHOOT;
				offshoot.start_time = start_time;
				offshoot.fireball_offshoot.start_time = start_time;
				offshoot.fireball_offshoot.start = t->fireball_shot.end;
				for (i16 dy = -1; dy <= 1; ++dy) {
					for (i16 dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0) {
							continue;
						}
						offshoot.fireball_offshoot.dir = V2_i16(dx, dy);
						offshoot.fireball_offshoot.cur_step = 0;
						offshoot.fireball_offshoot.num_steps = 3;

						transactions.append(offshoot);
					}
				}

				break;
			}
			case TRANSACTION_FIREBALL_OFFSHOOT: {
				u32 cur_step = t->fireball_offshoot.cur_step;
				u32 num_steps = t->fireball_offshoot.num_steps;
				v2_i16 dir = t->fireball_offshoot.dir;
				v2_i16 start = (v2_i16)t->fireball_offshoot.start;
				v2_i16 pos = start + (i16)cur_step * dir;

				u8 is_over = pos.x < 1 || pos.x > 254 || pos.y < 1 || pos.y > 254;
				is_over |= cur_step == num_steps;

				if (is_over) {
					Event e = {};
					e.type = EVENT_FIREBALL_OFFSHOOT;
					e.time = t->fireball_offshoot.start_time;
					e.fireball_offshoot.duration = (f32)cur_step
						/ constants.anims.fireball.min_speed;
					e.fireball_offshoot.start = t->fireball_offshoot.start;
					v2_i16 end = start + (i16)cur_step * dir;
					e.fireball_offshoot.end = (Pos)end;
					events.append(e);

					t->type = TRANSACTION_REMOVE;

					break;
				}

				u32 num_entities = game->entities.len;
				for (u32 i = 0; i < num_entities; ++i) {
					Entity *e = &game->entities[i];
					if (e->pos != (Pos)pos) {
						continue;
					}

					Entity_ID entity_id = e->id;
					u8 written = 0;
					for (u32 i = 0; i < entity_damage.len; ++i) {
						Entity_Damage *ed = &entity_damage[i];
						if (ed->entity_id == entity_id) {
							ed->damage += 1;
							written = 1;
							break;
						}
					}
					if (!written) {
						Entity_Damage ed = {};
						ed.entity_id = entity_id;
						ed.damage = 1;
						ed.pos = e->pos;
						entity_damage.append(ed);
					}
				}

				++t->fireball_offshoot.cur_step;
				t->start_time += 1.0f / constants.anims.fireball.min_speed;

				break;
			}
			case TRANSACTION_EXCHANGE_CAST: {
				// TODO -- add cast event
				t->type = TRANSACTION_EXCHANGE;
				t->start_time += constants.anims.exchange.cast_time;
				break;
			}
			case TRANSACTION_EXCHANGE: {
				t->type = TRANSACTION_REMOVE;
				Event event = {};
				event.type = EVENT_EXCHANGE;
				event.time = time;
				event.exchange.a = t->exchange.a;
				event.exchange.b = t->exchange.b;
				events.append(event);
				Entity *a = game_get_entity_by_id(game, t->exchange.a);
				Entity *b = game_get_entity_by_id(game, t->exchange.b);
				Pos tmp = a->pos;
				a->pos = b->pos;
				b->pos = tmp;
				break;
			}
			case TRANSACTION_BLINK_CAST: {
				// TODO -- add cast event
				// maybe should have things like "anti magic fields" etc?
				t->type = TRANSACTION_BLINK;
				t->start_time += constants.anims.blink.cast_time;
				break;
			}
			case TRANSACTION_BLINK: {
				t->type = TRANSACTION_REMOVE;

				Event event = {};
				event.type = EVENT_BLINK;
				event.time = time;
				event.blink.caster_id = t->blink.caster_id;
				event.blink.target = t->blink.target;
				events.append(event);

				Entity *e = game_get_entity_by_id(game, t->blink.caster_id);

				// TODO -- should check if pos is free to enter?
				Pos start = e->pos;
				Pos end = t->blink.target;

				occupied.unset(start);
				e->pos = end;
				occupied.set(end);

				Message m = {};
				m.type = MESSAGE_MOVE_POST_EXIT;
				m.move.entity_id = e->id;
				m.move.start = start;
				m.move.end = end;
				game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);

				m.type = MESSAGE_MOVE_POST_ENTER;
				game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);
				break;
			}
			case TRANSACTION_POISON_CAST: {
				t->type = TRANSACTION_POISON;
				t->start_time += constants.anims.poison.cast_time;
				break;
			}
			case TRANSACTION_POISON: {
				t->type = TRANSACTION_REMOVE;

				Entity_ID e_id = t->poison.entity_id;
				Entity *e = game_get_entity_by_id(game, e_id);
				ASSERT(e);

				// XXX -- this currently is a bug
				// should combine Entity_Damage events _here_ in current version
				// but will change to collapsing after this loop in future
				Entity_Damage damage = {};
				damage.entity_id = e_id;
				damage.damage = 3;
				damage.pos = e->pos;
				entity_damage.append(damage);

				break;
			}
			case TRANSACTION_SLIME_SPLIT: {
				t->type = TRANSACTION_REMOVE;

				Entity *e = game_get_entity_by_id(game, t->slime_split.slime_id);
				if (!e) {
					break;
				}
				Pos p = e->pos;
				v2_i16 start = (v2_i16)p;
				Max_Length_Array<Pos, 8> poss;
				poss.reset();
				for (i16 dy = -1; dy <= 1; ++dy) {
					for (i16 dx = -1; dx <= 1; ++dx) {
						if (!dx && !dy) {
							continue;
						}
						v2_i16 end = start + V2_i16(dx, dy);
						Pos pend = (Pos)end;
						if (game_is_passable(game, pend, BLOCK_WALK)) {
							poss.append((Pos)end);
						}
					}
				}
				if (poss) {
					Pos end = poss[rand_u32() % poss.len];
					Entity_ID new_id = game_add_slime(game,
					                                  end,
					                                  t->slime_split.hit_points);
					Event event = {};
					event.type = EVENT_SLIME_SPLIT;
					event.time = time;
					event.slime_split.original_id = e->id;
					event.slime_split.new_id = new_id;
					event.slime_split.start = (v2)start;
					event.slime_split.end = (v2)end;
					events.append(event);
				}
				break;
			}
			}
		}

		// process entity damage events
		for (u32 i = 0; i < entity_damage.len; ++i) {
			Entity_Damage *ed = &entity_damage[i];
			Entity_ID entity_id = ed->entity_id;

			u32 num_entities = game->entities.len;
			Entity *e = game->entities.items;
			for (u32 i = 0; i < num_entities; ++i, ++e) {
				if (e->id == entity_id) {
					break;
				}
			}
			ASSERT(e->id == entity_id);

			e->hit_points -= ed->damage;
			u8 entity_died = e->hit_points <= 0;
			if (entity_died) {
				Event death_event = {};
				death_event.type = EVENT_DEATH;
				death_event.time = time;
				death_event.death.entity_id = entity_id;
				events.append(death_event);
				// *e = game->entities[--game->num_entities];
				game_remove_entity(game, entity_id);
			}

			Message m = {};
			m.type = MESSAGE_DAMAGE;
			m.damage.entity_id = entity_id;
			m.damage.amount = ed->damage;
			m.damage.entity_died = entity_died;
			game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);

			Event event = {};
			event.type = EVENT_DAMAGED;
			event.time = time;
			event.damaged.entity_id = entity_id;
			event.damaged.amount = ed->damage;
			event.damaged.pos = ed->pos;
			events.append(event);
		}

		// remove finished transactions
		u32 push_back = 0;
		for (u32 i = 0; i < transactions.len; ++i) {
			if (transactions[i].type == TRANSACTION_REMOVE) {
				++push_back;
			} else if (push_back) {
				transactions[i - push_back] = transactions[i];
			}
		}
		transactions.len -= push_back;

		// TODO - need some better representation of "infinity"
		f32 next_time = 1000000.0f;
		for (u32 i = 0; i < transactions.len; ++i) {
			Transaction *t = &transactions[i];
			f32 start_time = t->start_time;
			ASSERT(start_time > time);
			if (start_time < next_time) {
				next_time = start_time;
			}
		}
		time = next_time;
	}
}

void game_do_turn(Game* game, Event_Buffer* event_buffer)
{
	event_buffer->reset();

	// =====================================================================
	// create chosen actions

	Max_Length_Array<Action, MAX_ENTITIES> actions;
	actions.reset();

	// 1. choose moves

	struct Potential_Move
	{
		Entity_ID entity_id;
		Pos start;
		Pos end;
		f32 weight;
	};
	Max_Length_Array<Potential_Move, MAX_ENTITIES * 9> potential_moves;
	potential_moves.reset();

	Map_Cache_Bool occupied;
	occupied.reset();

	Pos player_pos = game_get_player_pos(game);

	auto& tiles = game->tiles;

	u32 num_controllers = game->controllers.len;
	for (u32 i = 0; i < num_controllers; ++i) {
		Controller *c = &game->controllers[i];
		switch (c->type) {
		case CONTROLLER_PLAYER:
			if (c->player.action.type == ACTION_MOVE) {
				Potential_Move pm = {};
				pm.entity_id = c->player.entity_id;
				pm.start = c->player.action.move.start;
				pm.end = c->player.action.move.end;
				player_pos = pm.end;
				// XXX -- weight here should be infinite
				pm.weight = 10.0f;
				potential_moves.append(pm);
			}
			break;
		case CONTROLLER_RANDOM_MOVE: {
			Entity *e = game_get_entity_by_id(game, c->random_move.entity_id);
			u16 move_mask = e->movement_type;
			Pos start = e->pos;
			for (i8 dy = -1; dy <= 1; ++dy) {
				for (i8 dx = -1; dx <= 1; ++dx) {
					if (!(dx || dy)) {
						continue;
					}
					Pos end = (Pos)((v2_i16)start + V2_i16(dx, dy));
					Tile t = tiles[end];
					if (tile_is_passable(t, move_mask)) {
						Potential_Move pm = {};
						pm.entity_id = e->id;
						pm.start = start;
						pm.end = end;
						pm.weight = uniform_f32(0.0f, 1.0f);
						// pm.weight = 1.0f;
						potential_moves.append(pm);
					}
				}
			}
			break;
		}
		case CONTROLLER_SLIME: {
			Entity *e = game_get_entity_by_id(game, c->slime.entity_id);
			// Pos p = e->pos;
			v2_i16 start = (v2_i16)e->pos;
			v2_i16 iplayer_pos = (v2_i16)player_pos;
			v2_i16 d = iplayer_pos - start;
			u32 distance_squared = (u32)(d.x*d.x + d.y*d.y);
			for (i16 dy = -1; dy <= 1; ++dy) {
				for (i16 dx = -1; dx <= 1; ++dx) {
					if (!dx && !dy) {
						continue;
					}
					v2_i16 end = start + (V2_i16)(dx, dy);
					d = iplayer_pos - end;
					Pos pend = (Pos)end;
					Tile t = tiles[pend];
					if ((u32)(d.x*d.x + d.y*d.y) <= distance_squared
					 && tile_is_passable(t, e->movement_type)) {
						Potential_Move pm = {};
						pm.entity_id = e->id;
						pm.start = (Pos)start;
						pm.end = (Pos)end;
						pm.weight = uniform_f32(1.9f, 2.1f);
						potential_moves.append(pm);
					}
				}
			}
			break;
		}
		case CONTROLLER_RANDOM_SNAKE:
			break;
		}
	}

	u32 num_entities = game->entities.len;
	for (u32 i = 0; i < num_entities; ++i) {
		Entity *e = &game->entities[i];
		if (e->block_mask) {
			occupied.set(e->pos);
		}
	}

#if 0
	debug_draw_world_reset();
	debug_draw_world_set_color(V4_f32(1.0f, 0.0f, 0.0f, 1.0f));
	for (u8 y = 1; y < 255; ++y) {
		for (u8 x = 1; x < 255; ++x) {
			Pos p = { x, y };
			if (occupied.get(p)) {
				debug_draw_world_circle((v2)p, 0.25f);
			}
		}
	}
	debug_pause();
#endif

	// sort
	for (u32 i = 1; i < potential_moves.len; ++i) {
		Potential_Move tmp = potential_moves[i];
		u32 j = i;
		while (j && potential_moves[j - 1].weight < tmp.weight) {
			potential_moves[j] = potential_moves[j - 1];
			--j;
		}
		potential_moves[j] = tmp;
	}

	for (;;) {
		u32 idx = 0;
		while (idx < potential_moves.len && occupied.get(potential_moves[idx].end)) {
			++idx;
		}
		if (idx == potential_moves.len) {
			break;
		}
		Potential_Move pm = potential_moves[idx];

#ifdef DEBUG_CREATE_MOVES
		debug_draw_world_reset();
		debug_draw_world_set_color(V4_f32(pm.weight, 0.0f, 0.0f, 1.0f));
		debug_draw_world_arrow((v2)pm.start, (v2)pm.end);
		debug_pause();
#endif

		Action chosen_move = {};
		chosen_move.type = ACTION_MOVE;
		chosen_move.move.entity_id = pm.entity_id;
		chosen_move.move.start = pm.start;
		chosen_move.move.end = pm.end;
		actions.append(chosen_move);

		occupied.unset(pm.start);
		occupied.set(pm.end);

		u32 push_back = 0;
		for (u32 i = 0; i < potential_moves.len; ++i) {
			if (potential_moves[i].entity_id == pm.entity_id) {
				++push_back;
			} else if (push_back) {
				potential_moves[i - push_back] = potential_moves[i];
			}
		}
		potential_moves.len -= push_back;

#ifdef DEBUG_CREATE_MOVES
		debug_draw_world_reset();
		for (u32 i = 0; i < potential_moves.len; ++i) {
			Potential_Move pm = potential_moves[i];
			debug_draw_world_set_color(V4_f32(pm.weight, 0.0f, 0.0f, 1.0f));
			debug_draw_world_arrow((v2)pm.start, (v2)pm.end);
		}
		debug_pause();
#endif
	}

	// 2. create other actions...

	// XXX - the whole point of doing other actions _now_ is that we can
	// use the locations of where things finish after movement for targetting
	// we should incorporate that here
	for (u32 i = 0; i < num_controllers; ++i) {
		Controller *c = &game->controllers[i];
		switch (c->type) {
		case CONTROLLER_PLAYER:
			if (c->player.action.type != ACTION_MOVE) {
				actions.append(c->player.action);
			}
			break;
		case CONTROLLER_DRAGON: {
			Entity *e = game_get_entity_by_id(game, c->dragon.entity_id);
			u16 move_mask = e->movement_type;
			u32 t_idx = rand_u32() % game->entities.len;
			Entity *t = &game->entities[t_idx];
			Action a = {};
			a.type = ACTION_FIREBALL;
			a.fireball.start = e->pos;
			a.fireball.end = t->pos;

			/*
			debug_draw_world_reset();
			debug_draw_world_set_color(V4_f32(1.0f, 1.0f, 0.0f, 1.0f));
			debug_draw_world_arrow((v2)a.fireball.start, (v2)a.fireball.end);
			debug_pause();
			*/

			actions.append(a);
			break;
		}
		}
	}

#ifdef DEBUG_SHOW_ACTIONS
	debug_draw_world_reset();
	debug_draw_world_set_color(V4_f32(0.0f, 1.0f, 1.0f, 1.0f));
	for (u32 i = 0; i < actions.len; ++i) {
		Action action = actions[i];
		switch (action.type) {
		case ACTION_MOVE:
			debug_draw_world_arrow((v2)action.move.start, (v2)action.move.end);
			break;
		}
	}
	debug_pause();
#endif

	game_simulate_actions(game, actions, event_buffer);
}

void game_build_from_string(Game* game, char* str)
{
	memset(game, 0, sizeof(*game));

	Pos cur_pos = { 1, 1 };
	Entity_ID     e_id = 1;
	Controller_ID c_id = 1;

	Controller *player_controller = game->controllers.items;
	++game->controllers.len;
	player_controller->type = CONTROLLER_PLAYER;
	player_controller->id = c_id++;
	auto& tiles = game->tiles;
	auto& handlers = game->handlers;
	memset(&tiles, 0, sizeof(tiles));

	for (char *p = str; *p; ++p) {
		switch (*p) {
		case '\n':
			cur_pos.x = 1;
			++cur_pos.y;
			continue;
		case '#': {
			tiles[cur_pos].type = TILE_WALL;
			tiles[cur_pos].appearance = APPEARANCE_WALL_WOOD;
			break;
		}
		case 'x': {
			tiles[cur_pos].type = TILE_WALL;
			tiles[cur_pos].appearance = APPEARANCE_WALL_FANCY;
			break;
		}
		case '.': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;
			break;
		}
		case 'w': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 1;
			e.max_hit_points = 1;
			e.id = e_id++;
			e.pos = cur_pos;
			e.appearance = rand_u32() % 2 ?
			               APPEARANCE_ITEM_SPIDERWEB_1 : APPEARANCE_ITEM_SPIDERWEB_2;
			game->entities.append(e);

			Message_Handler mh = {};
			mh.type = MESSAGE_HANDLER_PREVENT_EXIT;
			mh.handle_mask = MESSAGE_MOVE_PRE_EXIT;
			mh.owner_id = e.id;
			mh.prevent_exit.pos = cur_pos;
			handlers.append(mh);

			break;
		}
		case 's': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			// XXX - ugly hack
			game->next_entity_id = e_id;
			game_add_slime(game, cur_pos, 5);
			e_id = game->next_entity_id;

			break;
		}
		case 'd': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 100;
			e.max_hit_points = 100;
			e.id = e_id++;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_CREATURE_RED_DRAGON;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.movement_type = BLOCK_WALK;
			game->entities.append(e);

			Controller c = {};
			c.id = c_id++;
			c.type = CONTROLLER_DRAGON;
			c.dragon.entity_id = e.id;

			game->controllers.append(c);

			break;
		}
		case '^': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 1;
			e.max_hit_points = 1;
			e.id = e_id++;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_ITEM_TRAP_HEX;
			game->entities.append(e);

			Message_Handler mh = {};
			mh.type = MESSAGE_HANDLER_TRAP_FIREBALL;
			mh.handle_mask = MESSAGE_MOVE_POST_ENTER;
			mh.owner_id = e.id;
			mh.trap.pos = cur_pos;
			handlers.append(mh);

			break;
		}
		case 'v': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Message_Handler mh = {};
			mh.type = MESSAGE_HANDLER_DROP_TILE;
			mh.handle_mask = MESSAGE_MOVE_POST_EXIT;
			mh.trap.pos = cur_pos;
			handlers.append(mh);

			break;
		}
		case '@': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 100;
			e.max_hit_points = 100;
			e.id = e_id++;
			e.pos = cur_pos;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.appearance = APPEARANCE_CREATURE_MALE_BERSERKER;
			e.movement_type = BLOCK_WALK;
			game->entities.append(e);

			player_controller->player.entity_id = e.id;

			break;
		}
		case 'b': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 5;
			e.max_hit_points = 5;
			e.id = e_id++;
			e.pos = cur_pos;
			e.movement_type = BLOCK_FLY;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.appearance = APPEARANCE_CREATURE_RED_BAT;
			game->entities.append(e);

			Controller c = {};
			c.id = c_id++;
			c.type = CONTROLLER_RANDOM_MOVE;
			c.random_move.entity_id = e.id;

			game->controllers.append(c);

			break;
		}
		case '~': {
			tiles[cur_pos].type = TILE_WATER;
			tiles[cur_pos].appearance = APPEARANCE_LIQUID_WATER;
			break;
		}
		}
		++cur_pos.x;
	}
	game->next_entity_id = e_id;
	game->next_controller_id = c_id;
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


// =============================================================================
// draw

struct Camera
{
	v2  world_center;
	v2  offset;
	f32 zoom;
};

struct Draw
{
	Camera camera;
	Sprite_Sheet_Renderer  renderer;
	Sprite_Sheet_Instances tiles;
	Sprite_Sheet_Instances creatures;
	Sprite_Sheet_Instances water_edges;
	Sprite_Sheet_Instances effects_32;
	Sprite_Sheet_Font_Instances boxy_bold;
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
	ANIM_MOVE_BLOCKED,
	ANIM_DROP_TILE,
	ANIM_PROJECTILE_EFFECT_32,
	ANIM_TEXT,
	ANIM_CAMERA_SHAKE,
	ANIM_SOUND,
	ANIM_DEATH,
	ANIM_EXCHANGE,
	ANIM_BLINK,
	ANIM_SLIME_SPLIT,
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
		struct {
			f32 start_time;
			f32 duration;
		} drop_tile;
		struct {
			f32 start_time;
			f32 duration;
			v2 start;
			v2 end;
		} projectile;
		struct {
			f32 start_time;
			f32 duration;
			u8 caption[16];
			v4 color;
		} text;
		struct {
			f32 start_time;
			f32 duration;
			f32 power;
		} camera_shake;
		struct {
			f32 start_time;
			Sound_ID sound_id;
		} sound;
		struct {
			f32 time;
			Entity_ID entity_id;
		} death;
		struct {
			f32 time;
			Entity_ID a, b;
		} exchange;
		struct {
			f32 time;
			Entity_ID entity_id;
			v2 target;
		} blink;
		struct {
			f32 time;
			f32 duration;
			Entity_ID original_id;
			Entity_ID new_id;
			v2 start;
			v2 end;
		} slime_split;
	};
	v2 sprite_coords;
	v2 world_coords;
	Entity_ID entity_id;
	f32 depth_offset;
};

u8 anim_is_active(Anim* anim)
{
	switch (anim->type) {
	case ANIM_TILE_STATIC:          return 0;
	case ANIM_WATER_EDGE:           return 0;
	case ANIM_CREATURE_IDLE:        return 0;
	case ANIM_MOVE:                 return 1;
	case ANIM_MOVE_BLOCKED:         return 1;
	case ANIM_DROP_TILE:            return 1;
	case ANIM_PROJECTILE_EFFECT_32: return 1;
	case ANIM_TEXT:                 return 1;
	case ANIM_CAMERA_SHAKE:         return 1;
	case ANIM_SOUND:                return 1;
	case ANIM_DEATH:                return 1;
	case ANIM_EXCHANGE:             return 1;
	case ANIM_BLINK:                return 1;
	case ANIM_SLIME_SPLIT:          return 1;
	}
	ASSERT(0);
	return 0;
}

#define MAX_ANIMS (5 * MAX_ENTITIES)
#define MAX_ANIM_BLOCKS 1024

struct World_Anim_State
{
	Max_Length_Array<Anim, MAX_ANIMS> anims;
	f32                               dynamic_anim_start_time;
	u32                               anim_block_number;
	u32                               event_buffer_idx;
	Event_Buffer                      events_to_be_animated;
	v2                                camera_offset;
};

u8 world_anim_is_animating(World_Anim_State *world_anim)
{
	auto& anims = world_anim->anims;
	for (u32 i = 0; i < anims.len; ++i) {
		if (anim_is_active(&anims[i])) {
			return 1;
		}
	}
	return 0;
}

void world_anim_build_events_to_be_animated(World_Anim_State* world_anim, Event_Buffer* event_buffer)
{
	u32 num_events = event_buffer->len;
	u32 new_blocks = 0;
	u32 cur_block_number = world_anim->anim_block_number;
	Event_Buffer *dest = &world_anim->events_to_be_animated;
	u8 prev_event_is_blocking = 1;
	for (u32 i = 0; i < num_events; ++i) {
		Event *cur_event = &event_buffer->items[i];
		u8 create_new_block = prev_event_is_blocking || event_type_is_blocking(cur_event->type);
		prev_event_is_blocking = event_type_is_blocking(cur_event->type);
		if (create_new_block) {
			++cur_block_number;
			++new_blocks;
		}
		cur_event->block_id = cur_block_number;
		dest->append(*cur_event);
	}
	world_anim->anim_block_number = cur_block_number;
}

void world_anim_animate_next_event_block(World_Anim_State* world_anim)
{
	u32 num_anims = world_anim->anims.len;
	u32 event_idx = world_anim->event_buffer_idx;
	u32 num_events = world_anim->events_to_be_animated.len;
	Event *events = world_anim->events_to_be_animated.items;

	if (event_idx >= num_events) {
		return;
	}

	u32 cur_block_id = events[event_idx].block_id;

	auto& anims = world_anim->anims;

	while (event_idx < num_events && events[event_idx].block_id == cur_block_id) {
		Event *event = &events[event_idx];
		switch (event->type) {
		case EVENT_MOVE: {
			for (u32 i = 0; i < anims.len; ++i) {
				Anim *anim = &world_anim->anims[i];
				if (anim->entity_id == event->move.entity_id) {
					anim->type = ANIM_MOVE;
					anim->move.duration = constants.anims.move.duration;
					anim->move.start_time = event->time;
					anim->move.start.x = (f32)event->move.start.x;
					anim->move.start.y = (f32)event->move.start.y;
					anim->move.end.x   = (f32)event->move.end.x;
					anim->move.end.y   = (f32)event->move.end.y;
				}
			}
			break;
		}
		case EVENT_MOVE_BLOCKED:
			for (u32 i = 0; i < anims.len; ++i) {
				Anim *anim = &world_anim->anims[i];
				if (anim->entity_id == event->move.entity_id) {
					anim->type = ANIM_MOVE_BLOCKED;
					anim->move.duration = constants.anims.move.duration;
					anim->move.start_time = event->time;
					anim->move.start.x = (f32)event->move.start.x;
					anim->move.start.y = (f32)event->move.start.y;
					anim->move.end.x   = (f32)event->move.end.x;
					anim->move.end.y   = (f32)event->move.end.y;
				}
			}
			break;
		case EVENT_DROP_TILE: {
			u32 tile_id = MAX_ENTITIES + pos_to_u16(event->drop_tile.pos);
			for (u32 i = 0; i < anims.len; ++i) {
				Anim *anim = &world_anim->anims[i];
				if (anim->entity_id == tile_id) {
					anim->type = ANIM_DROP_TILE;
					anim->drop_tile.duration = constants.anims.drop_tile.duration;
					anim->drop_tile.start_time = event->time;
				}
			}
			break;
		}
		case EVENT_FIREBALL_SHOT: {
			Anim a = {};
			a.type = ANIM_PROJECTILE_EFFECT_32;
			a.projectile.start_time = event->time;
			a.projectile.duration = constants.anims.fireball.shot_duration;
			a.projectile.start = (v2)event->fireball_shot.start;
			a.projectile.end = (v2)event->fireball_shot.end;
			anims.append(a);
			break;
		}
		case EVENT_FIREBALL_HIT: {
			Anim shake = {};
			shake.type = ANIM_CAMERA_SHAKE;
			shake.camera_shake.start_time = event->time;
			shake.camera_shake.duration = constants.anims.fireball.shake_duration;
			shake.camera_shake.power = constants.anims.fireball.shake_power;
			anims.append(shake);

			Anim sound = {};
			sound.type = ANIM_SOUND;
			sound.sound.start_time = event->time;
			sound.sound.sound_id = SOUND_FIRE_SPELL_13;
			anims.append(sound);
			break;
		}
		case EVENT_FIREBALL_OFFSHOOT: {
			Anim a = {};
			a.type = ANIM_PROJECTILE_EFFECT_32;
			a.projectile.start_time = event->time;
			a.projectile.duration = event->fireball_offshoot.duration;
			a.projectile.start = (v2)event->fireball_offshoot.start;
			a.projectile.end = (v2)event->fireball_offshoot.end;
			anims.append(a);
			break;
		}
		case EVENT_STUCK: {
			Anim text_anim = {};
			text_anim.type = ANIM_TEXT;
			text_anim.text.start_time = event->time;
			text_anim.text.duration = constants.anims.text_duration;
			text_anim.text.color = V4_f32(1.0f, 1.0f, 1.0f, 1.0f);
			text_anim.text.caption[0] = '*';
			text_anim.text.caption[1] = 's';
			text_anim.text.caption[2] = 't';
			text_anim.text.caption[3] = 'u';
			text_anim.text.caption[4] = 'c';
			text_anim.text.caption[5] = 'k';
			text_anim.text.caption[6] = '*';
			text_anim.text.caption[7] = 0;
			text_anim.world_coords = (v2)event->stuck.pos;
			world_anim->anims.append(text_anim);
			break;
		}
		case EVENT_DAMAGED: {
			Anim text_anim = {};
			text_anim.type = ANIM_TEXT;
			text_anim.text.start_time = event->time;
			text_anim.text.duration = constants.anims.text_duration;
			text_anim.text.color = V4_f32(1.0f, 0.0f, 0.0f, 1.0f);

			char buffer[20];
			snprintf(buffer, ARRAY_SIZE(buffer), "-%u", event->damaged.amount);
			u32 i = 0;
			for (char *p = buffer; *p; ++p, ++i) {
				text_anim.text.caption[i] = (u8)*p;
			}
			ASSERT(i < ARRAY_SIZE(text_anim.text.caption));
			text_anim.text.caption[i] = 0;
			text_anim.world_coords = (v2)event->damaged.pos;
			world_anim->anims.append(text_anim);
			break;
		}
		case EVENT_DEATH: {
			Anim anim = {};
			anim.type = ANIM_DEATH;
			anim.death.time = event->time;
			anim.death.entity_id = event->death.entity_id;
			world_anim->anims.append(anim);
			break;
		}
		case EVENT_EXCHANGE: {
			// TODO -- add anims for spell casting, sound of spell casting etc
			Anim ex = {};
			ex.type = ANIM_EXCHANGE;
			ex.exchange.time = event->time;
			ex.exchange.a = event->exchange.a;
			ex.exchange.b = event->exchange.b;
			world_anim->anims.append(ex);
			break;
		}
		case EVENT_BLINK: {
			// TODO -- spell casting sounds/anims/particle effect stuff
			Anim anim = {};
			anim.type = ANIM_BLINK;
			anim.blink.time = event->time;
			anim.blink.entity_id = event->blink.caster_id;
			anim.blink.target = (v2)event->blink.target;
			world_anim->anims.append(anim);
			break;
		}
		case EVENT_SLIME_SPLIT: {
			Anim anim = {};
			anim.depth_offset = constants.z_offsets.character;
			anim.type = ANIM_SLIME_SPLIT;
			anim.slime_split.time = event->time;
			anim.slime_split.duration = constants.anims.slime_split.duration;
			anim.slime_split.original_id = event->slime_split.original_id;
			anim.slime_split.new_id = event->slime_split.new_id;
			anim.slime_split.start = event->slime_split.start;
			anim.slime_split.end = event->slime_split.end;
			anim.world_coords = event->slime_split.end;
			anim.entity_id = event->slime_split.new_id;
			anim.sprite_coords = appearance_get_creature_sprite_coords(
				APPEARANCE_CREATURE_GREEN_SLIME);
			world_anim->anims.append(anim);
			break;
		}
		}
		++event_idx;
	}

	if (event_idx < num_events) {
		world_anim->event_buffer_idx = event_idx;
	} else {
		world_anim->events_to_be_animated.len = 0;
		world_anim->event_buffer_idx = 0;
	}
}

void world_anim_init(World_Anim_State* world_anim, Game* game)
{
	u32 anim_idx = 0;
	u32 num_entities = game->entities.len;

	for (u32 i = 0; i < num_entities; ++i) {
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
			ca.world_coords = (v2)pos;
			ca.entity_id = e->id;
			ca.depth_offset = constants.z_offsets.character;
			ca.idle.duration = 0.8f + 0.4f * rand_f32();
			ca.idle.offset = 0.0f;
			world_anim->anims.append(ca);
			continue;
		}
		if (appearance_is_floor(app)) {
			Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_floor_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = constants.z_offsets.floor;
			world_anim->anims.append(ta);
			continue;
		}
		if (appearance_is_liquid(app)) {
			Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_liquid_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = constants.z_offsets.floor;
			world_anim->anims.append(ta);
		}
		if (appearance_is_item(app)) {
			Anim ia = {};
			ia.type = ANIM_TILE_STATIC;
			ia.sprite_coords = appearance_get_item_sprite_coords(app);
			ia.world_coords = (v2)pos;
			ia.entity_id = e->id;
			ia.depth_offset = constants.z_offsets.item;
			world_anim->anims.append(ia);
			continue;
		}
	}

	auto& tiles = game->tiles;
	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			Pos p = V2_u8(x, y);
			Tile c = tiles[p];
			switch (c.type) {
			case TILE_EMPTY:
				break;
			case TILE_FLOOR: {
				v2 sprite_coords = appearance_get_floor_sprite_coords(c.appearance);
				Anim anim = {};
				anim.type = ANIM_TILE_STATIC;
				anim.sprite_coords = sprite_coords;
				anim.world_coords = (v2)p;
				anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
				anim.depth_offset = constants.z_offsets.floor;
				world_anim->anims.append(anim);
				break;
			}
			case TILE_WALL: {
				Appearance app = c.appearance;
				u8 tl = tiles[(Pos)((v2_i16)p + V2_i16(-1, -1))].appearance == app;
				u8 t  = tiles[(Pos)((v2_i16)p + V2_i16( 0, -1))].appearance == app;
				u8 tr = tiles[(Pos)((v2_i16)p + V2_i16( 1, -1))].appearance == app;
				u8 l  = tiles[(Pos)((v2_i16)p + V2_i16(-1,  0))].appearance == app;
				u8 r  = tiles[(Pos)((v2_i16)p + V2_i16( 1,  0))].appearance == app;
				u8 bl = tiles[(Pos)((v2_i16)p + V2_i16(-1,  1))].appearance == app;
				u8 b  = tiles[(Pos)((v2_i16)p + V2_i16( 0,  1))].appearance == app;
				u8 br = tiles[(Pos)((v2_i16)p + V2_i16( 1,  1))].appearance == app;

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
				anim.world_coords = (v2)p;
				anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
				anim.depth_offset = constants.z_offsets.wall;
				world_anim->anims.append(anim);

				if (tiles[(Pos)((v2_i16)p + V2_i16( 0,  1))].type != TILE_WALL) {
					anim.sprite_coords = { 30.0f, 36.0f };
					anim.world_coords = (v2)p + V2_f32(0.0f, 1.0f);
					anim.depth_offset = constants.z_offsets.wall_shadow;
					world_anim->anims.append(anim);
				}

				break;
			}
			case TILE_WATER: {
				v2 sprite_coords = appearance_get_liquid_sprite_coords(c.appearance);
				Anim anim = {};
				anim.type = ANIM_TILE_STATIC;
				anim.sprite_coords = sprite_coords;
				anim.world_coords = (v2)p;
				anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
				anim.depth_offset = constants.z_offsets.floor;
				world_anim->anims.append(anim);

				Tile_Type t = c.type;
				u8 mask = 0;
				if (tiles[(Pos)((v2_i16)p + V2_i16( 0, -1))].type == t) { mask |= 0x01; }
				if (tiles[(Pos)((v2_i16)p + V2_i16( 1, -1))].type == t) { mask |= 0x02; }
				if (tiles[(Pos)((v2_i16)p + V2_i16( 1,  0))].type == t) { mask |= 0x04; }
				if (tiles[(Pos)((v2_i16)p + V2_i16( 1,  1))].type == t) { mask |= 0x08; }
				if (tiles[(Pos)((v2_i16)p + V2_i16( 0,  1))].type == t) { mask |= 0x10; }
				if (tiles[(Pos)((v2_i16)p + V2_i16(-1,  1))].type == t) { mask |= 0x20; }
				if (tiles[(Pos)((v2_i16)p + V2_i16(-1,  0))].type == t) { mask |= 0x40; }
				if (tiles[(Pos)((v2_i16)p + V2_i16(-1, -1))].type == t) { mask |= 0x80; }

				if (~mask & 0xFF) {
					Anim anim = {};
					anim.type = ANIM_WATER_EDGE;
					anim.sprite_coords = { (f32)(mask % 16), (f32)(15 - mask / 16) };
					anim.world_coords = (v2)p;
					anim.entity_id = MAX_ENTITIES + pos_to_u16(p);
					anim.depth_offset = constants.z_offsets.water_edge;
					anim.water_edge.color = { 0x58, 0x80, 0xC0, 0xFF };
					world_anim->anims.append(anim);
				}
			}
			}

		}
	}
}

void world_anim_draw(World_Anim_State* world_anim, Draw* draw, Sound_Player* sound_player, f32 time)
{
	// reset draw state
	sprite_sheet_instances_reset(&draw->tiles);
	sprite_sheet_instances_reset(&draw->creatures);
	sprite_sheet_instances_reset(&draw->water_edges);
	sprite_sheet_instances_reset(&draw->effects_32);
	sprite_sheet_font_instances_reset(&draw->boxy_bold);

	f32 dyn_time = time - world_anim->dynamic_anim_start_time;

	// clear up finished animations
	// u32 num_anims = world_anim->anims.len;
	u8 active_anims_remaining = 0;
	auto& anims = world_anim->anims;
	for (u32 i = 0; i < anims.len; ) {
		Anim *anim = &world_anim->anims[i];
		switch (anim->type) {
		case ANIM_MOVE:
			if (anim->move.start_time + anim->move.duration <= dyn_time) {
				anim->world_coords = anim->move.end;
				anim->type = ANIM_CREATURE_IDLE;
				anim->idle.offset = time;
				anim->idle.duration = uniform_f32(0.8f, 1.2f);
			}
			break;
		case ANIM_MOVE_BLOCKED:
			if (anim->move.start_time + anim->move.duration <= dyn_time) {
				anim->world_coords = anim->move.start;
				anim->type = ANIM_CREATURE_IDLE;
				anim->idle.offset = time;
				anim->idle.duration = uniform_f32(0.8f, 1.2f);
			}
			break;
		case ANIM_DROP_TILE:
			if (anim->drop_tile.start_time + anim->drop_tile.duration <= dyn_time) {
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_PROJECTILE_EFFECT_32:
			if (anim->projectile.start_time + anim->projectile.duration <= dyn_time) {
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_TEXT:
			if (anim->text.start_time + anim->text.duration <= dyn_time) {
				anims.remove(i);
				continue;
			}
		case ANIM_CAMERA_SHAKE:
			if (anim->camera_shake.start_time + anim->camera_shake.duration <= dyn_time) {
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_SOUND:
			if (anim->sound.start_time <= dyn_time) {
				sound_player->play(anim->sound.sound_id);
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_DEATH:
			if (anim->death.time <= dyn_time) {
				Entity_ID entity_id = anim->death.entity_id;
				anims.remove(i);
				for (u32 i = 0; i < anims.len; ) {
					if (anims[i].entity_id == entity_id) {
						anims.remove(i);
						continue;
					}
					++i;
				}
				continue;
			}
			break;
		case ANIM_EXCHANGE:
			if (anim->exchange.time <= dyn_time) {
				Entity_ID a_id = anim->exchange.a;
				Entity_ID b_id = anim->exchange.b;
				Anim *a = NULL, *b = NULL;
				for (u32 i = 0; i < anims.len; ++i) {
					Anim *anim = &anims[i];
					if (anim->entity_id == a_id) {
						a = anim;
					}
					if (anim->entity_id == b_id) {
						b = anim;
					}
				}
				ASSERT(a && b);
				// XXX - not sure what to do about this
				ASSERT(a->type == ANIM_CREATURE_IDLE);
				ASSERT(b->type == ANIM_CREATURE_IDLE);
				v2 tmp = a->world_coords;
				a->world_coords = b->world_coords;
				b->world_coords = tmp;
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_BLINK:
			if (anim->blink.time <= dyn_time) {
				Entity_ID e_id = anim->blink.entity_id;
				Anim *caster_anim = anims.items;
				for (u32 i = 0; i < anims.len; ++i, ++caster_anim) {
					if (caster_anim->entity_id == e_id) {
						break;
					}
				}
				ASSERT(caster_anim->entity_id == e_id);
				caster_anim->world_coords = anim->blink.target;
				anims.remove(i);
			}
			break;
		case ANIM_SLIME_SPLIT:
			if (anim->slime_split.time + anim->slime_split.duration <= dyn_time) {
				anim->entity_id = anim->slime_split.new_id;
				anim->world_coords = anim->slime_split.end;
				anim->type = ANIM_CREATURE_IDLE;
				anim->idle.offset = time;
				anim->idle.duration = uniform_f32(0.8f, 1.2f);
			}
			break;
		}
		++i;
		if (anim_is_active(anim)) {
			active_anims_remaining = 1;
		}
	}

	// prepare next events if we're still animating
	if (!active_anims_remaining) {
		if (world_anim->event_buffer_idx < world_anim->events_to_be_animated.len) {
			world_anim_animate_next_event_block(world_anim);
			world_anim->dynamic_anim_start_time = time;
			dyn_time = 0.0f;
		} else {
			world_anim->event_buffer_idx = 0;
			world_anim->events_to_be_animated.reset();
		}
	}


	f32 camera_offset_mag = 0.0f;
	// draw tile animations
	for (u32 i = 0; i < anims.len; ++i) {
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
			ci.y_offset = -3.0f;
			ci.sprite_pos = { 4.0f, 22.0f };
			ci.world_pos = world_pos;
			ci.sprite_id = anim->entity_id;
			ci.depth_offset = anim->depth_offset;
			ci.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			sprite_sheet_instances_add(&draw->creatures, ci);

			v2 sprite_pos = anim->sprite_coords;
			f32 dt = time + anim->idle.offset;
			dt = fmodf(dt, anim->idle.duration) / anim->idle.duration;
			if (dt > 0.5f) {
				sprite_pos.y += 1.0f;
			}
			ci.sprite_pos = sprite_pos;
			ci.y_offset = -6.0f;
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
			ci.y_offset = -3.0f;
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
			ci.y_offset = -6.0f - constants.anims.move.jump_height * 4.0f * dt*(1.0f - dt);
			ci.world_pos = world_pos;
			ci.depth_offset += 0.5f;

			sprite_sheet_instances_add(&draw->creatures, ci);

			break;
		}
		case ANIM_MOVE_BLOCKED: {
			f32 dt = (dyn_time - anim->move.start_time) / anim->move.duration;
			v2 start = anim->move.start;
			v2 end = anim->move.end;
			v2 world_pos = {};
			f32 w = dt;
			if (dt > 0.5f) {
				w = 1.0f - dt;
			}
			world_pos.x = start.x + (end.x - start.x) * w;
			world_pos.y = start.y + (end.y - start.y) * w;

			Sprite_Sheet_Instance ci = {};

			// draw shadow
			ci.y_offset = -3.0f;
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
			ci.y_offset = -6.0f - constants.anims.move.jump_height * 4.0f * dt*(1.0f - dt);
			ci.world_pos = world_pos;
			ci.depth_offset += 0.5f;

			sprite_sheet_instances_add(&draw->creatures, ci);

			break;
		}
		case ANIM_DROP_TILE: {
			f32 dt = (dyn_time - anim->drop_tile.start_time) / anim->drop_tile.duration;
			dt = max(0.0f, dt);

			f32 dy = dt*dt;

			Sprite_Sheet_Instance ti = {};
			ti.sprite_pos = anim->sprite_coords;
			ti.world_pos = anim->world_coords;
			ti.y_offset = 24.0f * dy * constants.anims.drop_tile.drop_distance;
			ti.sprite_id = anim->entity_id;
			ti.depth_offset = anim->depth_offset;

			f32 c = 1.0f - dt;
			ti.color_mod = { c, c, c, 1.0f };
			sprite_sheet_instances_add(&draw->tiles, ti);

			break;
		}
		case ANIM_PROJECTILE_EFFECT_32: {
			f32 dt = (dyn_time - anim->projectile.start_time) / anim->projectile.duration;
			if (dt < 0.0f) {
				break;
			}
			// dt = dt * dt;

			Sprite_Sheet_Instance instance = {};
			instance.sprite_pos = anim->sprite_coords;
			instance.world_pos = lerp(anim->projectile.start, anim->projectile.end, dt);
			instance.sprite_id = 0;
			instance.depth_offset = 2.0f;
			instance.color_mod = V4_f32(1.0f, 1.0f, 1.0f, 1.0f);

			sprite_sheet_instances_add(&draw->effects_32, instance);
			break;
		}
		case ANIM_TEXT: {
			f32 dt = (dyn_time - anim->text.start_time) / anim->text.duration;
			if (dt < 0.0f) {
				break;
			}

			u32 width = 1, height = 0;
			for (u8 *p = anim->text.caption; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				width += glyph.dimensions.w - 1;
				height = max(height, glyph.dimensions.h);
			}

			Sprite_Sheet_Font_Instance instance = {};
			instance.world_pos = anim->world_coords + 0.5f;
			instance.world_pos.y -= dt;
			instance.world_offset = { -(f32)(width / 2), -(f32)(height / 2) };
			instance.zoom = 1.0f;
			instance.color_mod = anim->text.color;
			for (u8 *p = anim->text.caption; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				instance.glyph_pos = (v2)glyph.top_left;
				instance.glyph_size = (v2)glyph.dimensions;

				sprite_sheet_font_instances_add(&draw->boxy_bold, instance);

				instance.world_offset.x += (f32)glyph.dimensions.w - 1.0f;
			}

			break;
		}
		case ANIM_CAMERA_SHAKE: {
			f32 dt = (dyn_time - anim->camera_shake.start_time) / anim->camera_shake.duration;
			if (dt < 0.0f) {
				break;
			}

			f32 power = anim->camera_shake.power * (dt - 1.0f) * (dt - 1.0f);
			camera_offset_mag = max(camera_offset_mag, power);

			break;
		}
		case ANIM_SLIME_SPLIT: {
			f32 dt = (dyn_time - anim->slime_split.time) / anim->slime_split.duration;
			if (dt < 0.0f) {
				break;
			}
			v2 start = anim->slime_split.start;
			v2 end = anim->slime_split.end;
			v2 world_pos = lerp(start, end, dt);

			Sprite_Sheet_Instance ci = {};

			// draw shadow
			ci.y_offset = -3.0f;
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
			ci.y_offset = -6.0f - constants.anims.move.jump_height * 4.0f * dt*(1.0f - dt);
			ci.world_pos = world_pos;
			ci.depth_offset += 0.5f;

			sprite_sheet_instances_add(&draw->creatures, ci);

			break;
		}
		}
	}

	if (camera_offset_mag > 0.0f) {
		f32 theta = uniform_f32(0.0f, 2.0f * PI_F32);
		m2 m = m2::rotation(theta);
		v2 v = m * V2_f32(camera_offset_mag, 0.0f);
		world_anim->camera_offset = v;
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
	f32 origin_separation = params->separation;

	f32 theta;
	f32 radius;

	if (params->num_cards <= 1.0f) {
		theta = 0.0f;
		radius = 1.0f;
	} else {
		f32 theta_low = 0;
		f32 theta_high = PI_F32 / 2.0f;
		while (theta_high - theta_low > 1e-6f) {
			f32 theta_mid = (theta_high + theta_low) / 2.0f;
			f32 val = theta_mid / (1.0f - cosf(theta_mid / 2.0f));
			if (val < target) {
				theta_high = theta_mid;
			} else {
				theta_low = theta_mid;
			}
		}
		theta = (theta_low + theta_high) / 2.0f;
		radius = ((params->num_cards - 1.0f) * params->separation) / theta;

		f32 card_diagonal, psi;
		{
			f32 w = params->card_size.w, h = params->card_size.h;
			card_diagonal = sqrtf(w*w + h*h);

			psi = atanf(w / h);
		}

		if (radius * sinf(theta / 2.0f) + card_diagonal * sinf(theta / 2.0f + psi) > max_width) {
			target = max_width / h;
			theta_low = 0;
			theta_high = PI_F32 / 2.0f;
			while (theta_high - theta_low > 1e-6f) {
				f32 theta_mid = (theta_high + theta_low) / 2.0f;
				f32 val = sinf(theta_mid / 2.0f) / (1.0f - cosf(theta_mid / 2.0f))
					+ (card_diagonal / h) * sinf(theta_mid / 2.0f + psi);
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
	}

	if (radius < constants.cards_ui.min_radius) {
		radius = constants.cards_ui.min_radius;
		theta = params->separation * (params->num_cards - 1.0f) / radius;
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

struct Card_Hand_Pos
{
	f32 angle;
	f32 zoom;
	f32 radius;
	f32 angle_speed;
	f32 zoom_speed;
	f32 radius_speed;
};

enum Card_Pos_Type
{
	CARD_POS_DECK,
	CARD_POS_DISCARD,
	CARD_POS_HAND,
	CARD_POS_ABSOLUTE,
	CARD_POS_IN_PLAY,
};

struct Card_Pos
{
	Card_Pos_Type type;
	union {
		struct {
			f32 angle;
			f32 zoom;
			f32 radius;
		} hand;
		struct {
			v2 pos;
			f32 angle;
			f32 zoom;
		} absolute;
	};
	f32 z_offset;
};

enum Card_Anim_Type
{
	CARD_ANIM_DECK,
	CARD_ANIM_DISCARD,
	CARD_ANIM_DRAW,
	CARD_ANIM_IN_HAND,
	CARD_ANIM_IN_PLAY,
	CARD_ANIM_HAND_TO_HAND,
	CARD_ANIM_HAND_TO_IN_PLAY,
	CARD_ANIM_HAND_TO_DISCARD,
	CARD_ANIM_IN_PLAY_TO_DISCARD,
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
			u8 played_sound;
		} draw;
		struct {
			f32 start_time;
			f32 duration;
			Card_Hand_Pos start;
		} hand_to_in_play;
		struct {
			f32 start_time;
			f32 duration;
			Card_Hand_Pos start;
		} hand_to_discard;
		struct {
			f32 start_time;
			f32 duration;
			Card_Pos start;
		} in_play_to_discard;
	};
	Card_Pos pos;
	u32 card_id;
	v2 card_face;
};

#define MAX_CARD_ANIMS 1024
struct Card_Anim_State
{
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

// TODO -- finish hand to hand animations
struct Hand_To_Hand_Anim_Params
{
	Hand_Params hand_params;
	u32 highlighted_card_id;
	u32 selected_card_index;
};

void card_anim_write_poss(Card_Anim_State* card_anim_state, f32 time)
{
	f32 deltas[100];

	char buffer[1024];

	Hand_Params hand_params = card_anim_state->hand_params;
	u32 highlighted_card_id = card_anim_state->highlighted_card_id;
	u32 num_card_anims = card_anim_state->num_card_anims;
	u32 selected_card_index = card_anim_state->hand_size;
	Card_Anim *anim = card_anim_state->card_anims;
	snprintf(buffer, ARRAY_SIZE(buffer), "highlighted_card_id = %u", highlighted_card_id);
	log(debug_log, buffer);
	for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
		if (anim->card_id == highlighted_card_id) {
			switch (anim->type) {
			case CARD_ANIM_IN_HAND:
				selected_card_index = anim->hand.index;
				log(debug_log, "set selected_card_index 1");
				break;
			case CARD_ANIM_HAND_TO_HAND:
				log(debug_log, "set selected_card_index 2");
				selected_card_index = anim->hand_to_hand.index;
				break;
			default:
				break;
			}
			break;
		}
	}
	snprintf(buffer, ARRAY_SIZE(buffer), "num_card_anims: %u", num_card_anims);
	log(debug_log, buffer);
	if (selected_card_index < card_anim_state->hand_size) {
		log(debug_log, "selected_card_index < card_anim_state->hand_size");
		hand_calc_deltas(deltas, &hand_params, selected_card_index);
	} else {
		log(debug_log, "selected_card_index >= card_anim_state->hand_size");
		highlighted_card_id = 0;
		memset(deltas, 0, selected_card_index * sizeof(deltas[0]));
	}

	f32 base_delta = 0.0f;
	if (hand_params.num_cards > 1.0f) {
		base_delta = 1.0f / (hand_params.num_cards - 1.0f);
	}

	f32 ratio = hand_params.screen_width;

	anim = card_anim_state->card_anims;
	for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
		switch (anim->type) {
		case CARD_ANIM_DECK:
			anim->pos.type = CARD_POS_DECK;
			break;
		case CARD_ANIM_DISCARD:
			anim->pos.type = CARD_POS_DISCARD;
			break;
		case CARD_ANIM_DRAW: {
			v2 start_pos = V2_f32(-ratio + hand_params.border / 2.0f,
			                      -1.0f  + hand_params.height / 2.0f);
			f32 start_rotation = 0.0f;

			u32 hand_index = anim->draw.hand_index;
			f32 theta = hand_params.theta;
			f32 base_angle = PI_F32 / 2.0f + theta * (0.5f - (f32)hand_index * base_delta);

			f32 a = base_angle + deltas[hand_index];
			f32 r = hand_params.radius;

			v2 end_pos = r * V2_f32(cosf(a), sinf(a)) + hand_params.center;
			f32 end_rotation = a - PI_F32 / 2.0f;

			f32 dt = (time - anim->draw.start_time) / anim->draw.duration;
			dt = clamp(dt, 0.0f, 1.0f);
			f32 jump = dt * (1.0f - dt) * 4.0f;
			f32 smooth = (3.0f - 2.0f * dt) * dt * dt;

			if (dt < 0.5f) {
				anim->pos.z_offset = 1.0f;
			} else {
				anim->pos.z_offset = (f32)anim->draw.hand_index / hand_params.num_cards;
			}

			anim->pos.type = CARD_POS_ABSOLUTE;
			anim->pos.absolute.pos   = lerp(start_pos,      end_pos,      smooth);
			anim->pos.absolute.pos.y += jump * constants.cards_ui.draw_jump_height;
			anim->pos.absolute.angle = lerp(start_rotation, end_rotation, smooth);
			anim->pos.absolute.zoom = 1.0f;
			break;
		}
		case CARD_ANIM_IN_HAND: {
			u32 hand_index = anim->hand.index;
			f32 theta = hand_params.theta;
			f32 base_angle = PI_F32 / 2.0f + theta * (0.5f - (f32)hand_index * base_delta);

			anim->pos.z_offset = (f32)anim->hand.index / hand_params.num_cards;
			anim->pos.type = CARD_POS_HAND;
			anim->pos.hand.angle = base_angle + deltas[hand_index];
			anim->pos.hand.radius = hand_params.radius;
			anim->pos.hand.zoom = hand_index == selected_card_index && highlighted_card_id
			                    ? hand_params.highlighted_zoom : 1.0f;
			break;
		}
		case CARD_ANIM_HAND_TO_HAND: {
			f32 dt = (time - anim->hand_to_hand.start_time) / anim->hand_to_hand.duration;
			Card_Hand_Pos *s = &anim->hand_to_hand.start;
			Card_Hand_Pos *e = &anim->hand_to_hand.end;
			dt = clamp(dt, 0.0f, 1.0f);
			anim->pos.type = CARD_POS_HAND;
			anim->pos.hand.angle  = lerp(s->angle,  e->angle,  dt);
			anim->pos.hand.zoom   = lerp(s->zoom,   e->zoom,   dt);
			anim->pos.hand.radius = lerp(s->radius, e->radius, dt);
			anim->pos.z_offset = (f32)anim->hand_to_hand.index / hand_params.num_cards;
			break;
		}
		case CARD_ANIM_HAND_TO_IN_PLAY: {
			anim->pos.type = CARD_POS_ABSOLUTE;

			f32 a = anim->hand_to_in_play.start.angle;
			f32 r = anim->hand_to_in_play.start.radius;
			f32 z = anim->hand_to_in_play.start.zoom;
			f32 r2 = r + (z - 1.0f) * hand_params.card_size.h;

			v2 start_pos = r2 * V2_f32(cosf(a), sinf(a));
			start_pos.x += hand_params.center.x;
			start_pos.y += hand_params.top - r;
			f32 start_angle = a - PI_F32 / 2.0f;
			f32 start_zoom = z;

			v2 end_pos = V2_f32(ratio - hand_params.border / 2.0f,
			                    -1.0f + 3.0f * hand_params.height / 3.0f);
			f32 end_angle = 0.0f;
			f32 end_zoom = 1.0f;

			f32 dt = (time - anim->hand_to_in_play.start_time) / anim->hand_to_in_play.duration;
			dt = clamp(dt, 0.0f, 1.0f);
			anim->pos.absolute.pos   = lerp(start_pos,   end_pos,   dt);
			anim->pos.absolute.angle = lerp(start_angle, end_angle, dt);
			anim->pos.absolute.zoom  = lerp(start_zoom,  end_zoom,  dt);
			break;
		}
		case CARD_ANIM_HAND_TO_DISCARD: {
			anim->pos.type = CARD_POS_ABSOLUTE;

			f32 a = anim->hand_to_discard.start.angle;
			f32 r = anim->hand_to_discard.start.radius;
			f32 z = anim->hand_to_discard.start.zoom;
			f32 r2 = r + (z - 1.0f) * hand_params.card_size.h;

			v2 start_pos = r2 * V2_f32(cosf(a), sinf(a));
			start_pos.x += hand_params.center.x;
			start_pos.y += hand_params.top - r;
			f32 start_angle = a - PI_F32 / 2.0f;
			f32 start_zoom = z;

			v2 end_pos = V2_f32(ratio - hand_params.border / 2.0f,
			                    -1.0f + hand_params.height / 2.0f);
			f32 end_angle = 0.0f;
			f32 end_zoom = 1.0f;

			f32 dt = (time - anim->hand_to_in_play.start_time) / anim->hand_to_in_play.duration;
			dt = clamp(dt, 0.0f, 1.0f);
			anim->pos.absolute.pos   = lerp(start_pos,   end_pos,   dt);
			anim->pos.absolute.angle = lerp(start_angle, end_angle, dt);
			anim->pos.absolute.zoom  = lerp(start_zoom,  end_zoom,  dt);
			break;
		}
		case CARD_ANIM_IN_PLAY_TO_DISCARD: {
			anim->pos.type = CARD_POS_ABSOLUTE;

			v2  start_pos   = anim->in_play_to_discard.start.absolute.pos;
			f32 start_angle = anim->in_play_to_discard.start.absolute.angle;
			f32 start_zoom  = anim->in_play_to_discard.start.absolute.zoom;

			v2 end_pos = V2_f32(ratio - hand_params.border / 2.0f,
			                    -1.0f + hand_params.height / 2.0f);
			f32 end_angle = 0.0f;
			f32 end_zoom = 1.0f;

			f32 dt = (time - anim->in_play_to_discard.start_time) / anim->in_play_to_discard.duration;
			dt = clamp(dt, 0.0f, 1.0f);
			anim->pos.absolute.pos   = lerp(start_pos,   end_pos,   dt);
			anim->pos.absolute.angle = lerp(start_angle, end_angle, dt);
			anim->pos.absolute.zoom  = lerp(start_zoom,  end_zoom,  dt);

			break;
		}
		}
	}
}

void card_anim_create_hand_to_hand_anims(Card_Anim_State* card_anim_state,
                                         Hand_To_Hand_Anim_Params* params,
                                         f32 time)
{
	f32 deltas[100];

	u32 highlighted_card_id = params->highlighted_card_id;
	u32 highlighted_card = params->selected_card_index;

	if (highlighted_card_id) {
		hand_calc_deltas(deltas, &params->hand_params, params->selected_card_index);
	} else {
		memset(deltas, 0, (u32)params->hand_params.num_cards * sizeof(deltas[0]));
	}

	f32 base_delta;
	if (params->hand_params.num_cards > 1.0f) {
		base_delta = 1.0f / (params->hand_params.num_cards - 1.0f);
	} else {
		base_delta = 0.0f;
	}

	u32 num_card_anims = card_anim_state->num_card_anims;
	for (u32 i = 0; i < num_card_anims; ++i) {
		Card_Anim *anim = &card_anim_state->card_anims[i];
		if (anim->pos.type != CARD_POS_HAND) {
			continue;
		}
		u32 hand_index;
		switch (anim->type) {
		case CARD_ANIM_IN_HAND:
			hand_index = anim->hand.index;
			break;
		case CARD_ANIM_HAND_TO_HAND:
			hand_index = anim->hand_to_hand.index;
			break;
		default:
			// ASSERT(0);
			continue;
		}
		Card_Hand_Pos before_pos = {};
		before_pos.radius = anim->pos.hand.radius;
		before_pos.angle = anim->pos.hand.angle;
		before_pos.zoom = anim->pos.hand.zoom;

		f32 base_angle = PI_F32 / 2.0f + params->hand_params.theta
		               * (0.5f - (f32)hand_index * base_delta);
		Card_Hand_Pos after_pos = {};
		after_pos.angle = base_angle + deltas[hand_index];
		after_pos.zoom = hand_index == highlighted_card && highlighted_card_id
		               ? params->hand_params.highlighted_zoom : 1.0f;
		after_pos.radius = params->hand_params.radius;
		anim->type = CARD_ANIM_HAND_TO_HAND;
		anim->hand_to_hand.start_time = time;
		anim->hand_to_hand.duration = constants.cards_ui.hand_to_hand_time;
		anim->hand_to_hand.start = before_pos;
		anim->hand_to_hand.end = after_pos;
		anim->hand_to_hand.index = hand_index;
	}
}

void card_anim_update_anims(Card_Anim_State*  card_anim_state,
                            Slice<Card_Event> events,
                            f32               time)
{
	card_anim_write_poss(card_anim_state, time);

	Hand_To_Hand_Anim_Params before_params = {};
	before_params.hand_params = card_anim_state->hand_params;
	before_params.highlighted_card_id = 0;
	before_params.selected_card_index = 0;

	u32 highlighted_card_id = card_anim_state->highlighted_card_id;
	if (highlighted_card_id) {
		u32 highlighted_card_index = 0;
		u32 num_card_anims = card_anim_state->num_card_anims;
		Card_Anim *ca = card_anim_state->card_anims;
		for (u32 i = 0; i < num_card_anims; ++i, ++ca) {
			if (ca->card_id == highlighted_card_id) {
				switch (ca->type) {
				case CARD_ANIM_IN_HAND:
					before_params.highlighted_card_id = highlighted_card_id;
					before_params.selected_card_index = ca->hand.index;
					break;
				case CARD_ANIM_HAND_TO_HAND:
					before_params.highlighted_card_id = highlighted_card_id;
					before_params.selected_card_index = ca->hand_to_hand.index;
					break;
				}
				break;
			}
		}
	}

	u8 do_hand_to_hand = 0;

	f32 next_draw_time = 0.0f;
	for (u32 i = 0; i < events.len; ++i) {
		Card_Event *event = &events[i];
		switch (event->type) {
		case CARD_EVENT_DRAW: {
			do_hand_to_hand = 1;
			u32 hand_index = card_anim_state->hand_size++;
			Card_Anim anim = {};
			anim.type = CARD_ANIM_DRAW;
			anim.draw.start_time = time + next_draw_time;
			next_draw_time += constants.cards_ui.between_draw_delay;
			anim.draw.duration = constants.cards_ui.draw_duration;
			anim.draw.hand_index = hand_index;
			anim.draw.played_sound = 0;
			anim.card_id = event->draw.card_id;
			anim.card_face = card_appearance_get_sprite_coords(event->draw.appearance);
			card_anim_state->card_anims[card_anim_state->num_card_anims++] = anim;
			break;
		}
		case CARD_EVENT_HAND_TO_IN_PLAY: {
			do_hand_to_hand = 1;
			Card_Anim *anim = card_anim_state->card_anims;
			u32 num_card_anims = card_anim_state->num_card_anims;
			u32 hand_index = 0;
			for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
				if (anim->card_id == event->hand_to_in_play.card_id) {
					switch (anim->type) {
					case CARD_ANIM_IN_HAND:
						hand_index = anim->hand.index;
						break;
					case CARD_ANIM_HAND_TO_HAND:
						hand_index = anim->hand_to_hand.index;
						break;
					}
					anim->type = CARD_ANIM_HAND_TO_IN_PLAY;
					anim->hand_to_in_play.start_time = time;
					anim->hand_to_in_play.duration
					    = constants.cards_ui.hand_to_in_play_time;
					Card_Pos cp = anim->pos;
					ASSERT(cp.type == CARD_POS_HAND);
					anim->hand_to_in_play.start.angle  = cp.hand.angle;
					anim->hand_to_in_play.start.zoom   = cp.hand.zoom;
					anim->hand_to_in_play.start.radius = cp.hand.radius;
					break;
				}
			}
			anim = card_anim_state->card_anims;
			for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
				switch (anim->type) {
				case CARD_ANIM_IN_HAND:
					if (anim->hand.index > hand_index) {
						--anim->hand.index;
					}
					break;
				case CARD_ANIM_HAND_TO_HAND:
					if (anim->hand_to_hand.index > hand_index) {
						--anim->hand_to_hand.index;
					}
					break;
				}
			}
			--card_anim_state->hand_size;
			break;
		}
		case CARD_EVENT_HAND_TO_DISCARD: {
			do_hand_to_hand = 1;
			Card_Anim *anim = card_anim_state->card_anims;
			u32 num_card_anims = card_anim_state->num_card_anims;
			u32 hand_index = 0;
			for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
				if (anim->card_id == event->hand_to_discard.card_id) {
					switch (anim->type) {
					case CARD_ANIM_IN_HAND:
						hand_index = anim->hand.index;
						break;
					case CARD_ANIM_HAND_TO_HAND:
						hand_index = anim->hand_to_hand.index;
						break;
					}
					anim->type = CARD_ANIM_HAND_TO_DISCARD;
					anim->hand_to_discard.start_time = time;
					anim->hand_to_discard.duration
					    = constants.cards_ui.hand_to_discard_time;
					Card_Pos cp = anim->pos;
					ASSERT(cp.type == CARD_POS_HAND);
					anim->hand_to_discard.start.angle  = cp.hand.angle;
					anim->hand_to_discard.start.zoom   = cp.hand.zoom;
					anim->hand_to_discard.start.radius = cp.hand.radius;
					break;
				}
			}
			anim = card_anim_state->card_anims;
			for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
				switch (anim->type) {
				case CARD_ANIM_IN_HAND:
					if (anim->hand.index > hand_index) {
						--anim->hand.index;
					}
					break;
				case CARD_ANIM_HAND_TO_HAND:
					if (anim->hand_to_hand.index > hand_index) {
						--anim->hand_to_hand.index;
					}
					break;
				}
			}
			--card_anim_state->hand_size;
			break;
		}
		case CARD_EVENT_IN_PLAY_TO_DISCARD: {
			Card_Anim *anim = card_anim_state->card_anims;
			u32 num_card_anims = card_anim_state->num_card_anims;
			for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
				if (anim->card_id == event->in_play_to_discard.card_id) {
					anim->type = CARD_ANIM_IN_PLAY_TO_DISCARD;
					anim->in_play_to_discard.start_time = time;
					anim->in_play_to_discard.duration
					    = constants.cards_ui.in_play_to_discard_time;
					Card_Pos cp = anim->pos;
					ASSERT(cp.type == CARD_POS_ABSOLUTE);
					anim->in_play_to_discard.start = cp;
					break;
				}
			}
			break;
		}
		}
	}

	if (do_hand_to_hand) {
		Hand_Params hand_params = card_anim_state->hand_params;
		hand_params.num_cards = (f32)card_anim_state->hand_size;
		hand_params_calc(&hand_params);

		Hand_To_Hand_Anim_Params after_params = {};
		after_params.hand_params = hand_params;
		after_params.highlighted_card_id = 0;
		after_params.selected_card_index = 0;

		card_anim_create_hand_to_hand_anims(card_anim_state,
		                                    &after_params,
		                                    time);
	}
}

Card_UI_Event card_anim_draw(Card_Anim_State* card_anim_state,
                             Card_State*      card_state,
                             Card_Render*     card_render,
                             Sound_Player*    sound_player,
                             v2_u32           screen_size,
                             f32              time,
                             Input*           input,
                             u8               allow_highlight_card)
{
	const u32 discard_id = (u32)-1;
	const u32 deck_id    = (u32)-2;
	u32 num_card_anims = card_anim_state->num_card_anims;

	card_anim_write_poss(card_anim_state, time);

	Card_UI_Event event = {};
	event.type = CARD_UI_EVENT_NONE;

	// TODO -- get card id from mouse pos
	v2 card_mouse_pos = (v2)input->mouse_pos / (v2)screen_size;
	card_mouse_pos.x = (card_mouse_pos.x * 2.0f - 1.0f) * ((f32)screen_size.x / (f32)screen_size.y);
	card_mouse_pos.y = 1.0f - 2.0f * card_mouse_pos.y;
	u32 highlighted_card_id = 0;
	if (allow_highlight_card) {
		highlighted_card_id = card_render_get_card_id_from_mouse_pos(card_render, card_mouse_pos);
	}

	if (highlighted_card_id && input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
		if (highlighted_card_id == discard_id) {
			event.type = CARD_UI_EVENT_DISCARD_CLICKED;
		} else if (highlighted_card_id == deck_id) {
			event.type = CARD_UI_EVENT_DECK_CLICKED;
		} else {
			Card_Anim *anim = card_anim_state->card_anims;
			for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
				if (anim->card_id == highlighted_card_id) {
					switch (anim->type) {
					case CARD_ANIM_IN_HAND:
					case CARD_ANIM_HAND_TO_HAND:
						event.type = CARD_UI_EVENT_HAND_CLICKED;
						event.hand.card_id = highlighted_card_id;
						break;
					}
					break;
				}
			}
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
		case CARD_ANIM_HAND_TO_IN_PLAY:
			if (anim->hand_to_in_play.start_time + anim->hand_to_in_play.duration <= time) {
				// TODO -- convert hand_to_in_play anim to in_play anim
				// anim->type = CARD_ANIM_IN_PLAY;
			}
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

	u8 hi_or_prev_hi_was_hand = 0;
	{
		Card_Anim *anim = card_anim_state->card_anims;
		for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
			if (anim->card_id == highlighted_card_id
			 || anim->card_id == prev_highlighted_card_id) {
				switch (anim->type) {
				case CARD_ANIM_HAND_TO_HAND:
				case CARD_ANIM_IN_HAND:
					hi_or_prev_hi_was_hand = 1;
					goto break_loop;
				}
			}
		}
	break_loop: ;
	}

	if (prev_highlighted_card_id != highlighted_card_id && hi_or_prev_hi_was_hand) {

		Hand_To_Hand_Anim_Params before_params = {};
		before_params.hand_params = *params;
		before_params.highlighted_card_id = prev_highlighted_card_id;
		before_params.selected_card_index = prev_highlighted_card;

		Hand_To_Hand_Anim_Params after_params = {};
		after_params.hand_params = *params;
		after_params.highlighted_card_id = highlighted_card_id;
		after_params.selected_card_index = highlighted_card;

		card_anim_create_hand_to_hand_anims(card_anim_state,
		                                    &after_params,
		                                    time);
	}

	u8 is_a_card_highlighted = card_anim_state->highlighted_card_id
	                        && card_anim_state->highlighted_card_id < (u32)-2;
	f32 deltas[100];
	if (is_a_card_highlighted) {
		hand_calc_deltas(deltas, params, highlighted_card);
	} else {
		memset(deltas, 0, (u32)params->num_cards * sizeof(deltas[0]));
	}

	Card_Anim *anim = card_anim_state->card_anims;
	for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
		Card_Render_Instance instance = {};
		switch (anim->pos.type) {
		case CARD_POS_DECK:
		case CARD_POS_DISCARD:
			continue;
		case CARD_POS_HAND: {
			f32 a = anim->pos.hand.angle;
			f32 r = anim->pos.hand.radius;
			f32 z = anim->pos.hand.zoom;
			f32 r2 = r + (z - 1.0f) * params->card_size.h;

			v2 pos = r2 * V2_f32(cosf(a), sinf(a));
			pos.y += params->top - r;

			instance.screen_rotation = anim->pos.hand.angle - PI_F32 / 2.0f;
			instance.screen_pos = pos;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = anim->pos.z_offset;
			instance.zoom = z;
			break;
		}
		case CARD_POS_ABSOLUTE:
			instance.screen_rotation = anim->pos.absolute.angle;
			instance.screen_pos = anim->pos.absolute.pos;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = anim->pos.z_offset;
			instance.zoom = anim->pos.absolute.zoom;
			break;
		case CARD_POS_IN_PLAY:
			break;
		}
		switch (anim->type) {
		case CARD_ANIM_DRAW: {
			f32 dt = (time - anim->draw.start_time) / anim->draw.duration;
			if (dt < 0.0f) {
				break;
			}
			if (!anim->draw.played_sound) {
				anim->draw.played_sound = 1;
				Sound_ID sound = (Sound_ID)(SOUND_CARD_GAME_MOVEMENT_DEAL_SINGLE_01
				               + (rand_u32() % 3));
				sound_player->play(sound);
			}
			instance.horizontal_rotation = (1.0f - dt) * PI_F32;
			card_render_add_instance(card_render, instance);
			break;
		}
		default:
			card_render_add_instance(card_render, instance);
			break;
		}
	}

	// add draw pile card
	if (card_state->deck) {
		Card_Render_Instance instance = {};
		instance.screen_rotation = 0.0f;
		instance.screen_pos = { -ratio + params->border / 2.0f,
		                        -1.0f  + params->height / 2.0f };
		instance.card_pos = { 0.0f, 0.0f };
		instance.zoom = 1.0f;
		instance.card_id = deck_id;
		instance.z_offset = 0.0f;
		card_render_add_instance(card_render, instance);
	}

	// add discard pile card
	if (card_state->discard) {
		Card_Render_Instance instance = {};
		instance.screen_rotation = 0.0f;
		instance.screen_pos = { ratio - params->border / 2.0f,
		                        -1.0f + params->height / 2.0f };
		instance.card_pos = { 1.0f, 0.0f };
		instance.zoom = 1.0f;
		instance.card_id = discard_id;
		instance.z_offset = 0.0f;
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

#define MAX_CARD_PARAMS 32
enum Card_Param_Type
{
	CARD_PARAM_TARGET,
	CARD_PARAM_CREATURE,
	CARD_PARAM_AVAILABLE_TILE,
};

struct Card_Param
{
	Card_Param_Type type;
	union {
		struct {
			Pos *dest;
		} target;
		struct {
			Entity_ID *id;
		} creature;
		struct {
			Entity_ID  entity_id;
			Pos       *dest;
		} available_tile;
	};
};

struct Program
{
	Platform_Functions platform_functions;

	Program_State       state;
	volatile u32        process_frame_signal;
	volatile u32        debug_resume;

	Log                 debug_log;

	Input  *cur_input;
	v2_u32  cur_screen_size;

	u8                                 display_debug_ui;
	Game                               game;

	Stack<Program_Input_State, MAX_PROGRAM_INPUT_STATES> program_input_state_stack;
	Stack<Card_Param, MAX_CARD_PARAMS> card_params_stack;
	Action                             action_being_built;

	World_Anim_State                   world_anim;
	Draw                               draw;
	Sound_Player                       sound;

	u32           frame_number;
	v2            screen_size;
	v2_u32        max_screen_size;
	IMGUI_Context imgui;

	MT19937 random_state;

	u32 prev_card_id;

	Card_Anim_State card_anim_state;

	Debug_Draw_World debug_draw_world;
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
	debug_log = &program->debug_log;

	program->random_state.set_current();
	program->debug_draw_world.set_current();

#define PLATFORM_FUNCTION(_return_type, name, ...) name = program->platform_functions.name;
	PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION
}

void build_level_default(Program* program)
{
	game_build_from_string(&program->game,
		"##########################################\n"
		"#.........b...#b#........................#\n"
		"#......#.bwb..#b#........................#\n"
		"#.....###.b...#b#.......###..............#\n"
		"#....#####....#b#.......#x#.......d......#\n"
		"#...##.#.##...#b#.....###x#.#............#\n"
		"#..##..#..##..#b#.....#xxxxx#.......d....#\n"
		"#...b..#......#b#.....###x###.....d......#\n"
		"#b.bwb.#......#w#.......#x#..............#\n"
		"#wb.b..#...b..@.........###..............#\n"
		"#b.....#..bwb.#..........................#\n"
		"#......#...b..#vvv.^..^....#.............#\n"
		"#......#......#vvv.......................#\n"
		"#~~~...#.....x#vvv.^...^.....#...........#\n"
		"#~~~~..#.....x#vvv.......................#\n"
		"#~~~~~~..xxxxx#............#...#.........#\n"
		"##########x####..........................#\n"
		"#.............#........#.................#\n"
		"#.............#.......##.................#\n"
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

	program->draw.camera.zoom = 14.0f;
	Pos player_pos = game_get_player_pos(&program->game);
	program->draw.camera.world_center = (v2)player_pos;

	memset(&program->world_anim, 0, sizeof(program->world_anim));
	world_anim_init(&program->world_anim, &program->game);
}

void build_level_anim_test(Program* program)
{
	game_build_from_string(&program->game,
		"###########\n"
		"#b#b#b#b#b#\n"
		"#.#.#.#.#.#\n"
		"#^#^#^#^#^#\n"
		"#.#.#.#.#.#\n"
		"#....@....#\n"
		"###########\n");

	program->draw.camera.zoom = 14.0f;
	Pos player_pos = game_get_player_pos(&program->game);
	program->draw.camera.world_center = (v2)player_pos;

	memset(&program->world_anim, 0, sizeof(program->world_anim));
	world_anim_init(&program->world_anim, &program->game);
}

void build_level_slime_test(Program* program)
{
	game_build_from_string(&program->game,
		"##############################\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#.............s..............#\n"
		"#............................#\n"
		"#........s........s..........#\n"
		"#............................#\n"
		"#............................#\n"
		"#......s......@......s.......#\n"
		"#............................#\n"
		"#............................#\n"
		"#........s.........s.........#\n"
		"#.............s..............#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"##############################\n");

	program->draw.camera.zoom = 14.0f;
	Pos player_pos = game_get_player_pos(&program->game);
	program->draw.camera.world_center = (v2)player_pos;

	memset(&program->world_anim, 0, sizeof(program->world_anim));
	world_anim_init(&program->world_anim, &program->game);
}

void build_deck_random_100(Program *program)
{
	// cards
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->card_anim_state;
	memset(card_state, 0, sizeof(*card_state));
	memset(card_anim_state, 0, sizeof(*card_anim_state));

	for (u32 i = 0; i < 100; ++i) {
		Card card = {};
		card.id = i + 1;
		card.appearance = (Card_Appearance)(rand_u32() % NUM_CARD_APPEARANCES);
		card_state->discard.append(card);
	}
}

void build_deck_poison(Program *program)
{
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->card_anim_state;
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

void program_init(Program* program, Platform_Functions platform_functions)
{
	memset(program, 0, sizeof(*program));

#define PLATFORM_FUNCTION(_return_type, name, ...) program->platform_functions.name = platform_functions.name;
	PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION

	set_global_state(program);
	program->state = PROGRAM_STATE_NO_PAUSE;
	program->random_state.seed(0);

	sprite_sheet_renderer_init(&program->draw.renderer,
	                           &program->draw.tiles, 4,
	                           { 1600, 900 });


	program->draw.tiles.data         = SPRITE_SHEET_TILES;
	program->draw.creatures.data     = SPRITE_SHEET_CREATURES;
	program->draw.water_edges.data   = SPRITE_SHEET_WATER_EDGES;
	program->draw.effects_32.data    = SPRITE_SHEET_EFFECTS_32;
	program->draw.boxy_bold.tex_size = boxy_bold_texture_size;
	program->draw.boxy_bold.tex_data = boxy_bold_pixel_data;

	build_level_default(program);
	build_deck_random_100(program);

	program->sound.set_ambience(SOUND_CARD_GAME_AMBIENCE_CAVE);

	program->state = PROGRAM_STATE_NORMAL;
	program->program_input_state_stack.push(GIS_NONE);

	start_thread(load_assets, program);
}

u8 program_dsound_init(Program* program, IDirectSound* dsound)
{
	return sound_player_dsound_init(&program->sound, &program->assets_header, dsound);
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

	if (!sprite_sheet_instances_d3d11_init(&program->draw.effects_32, device)) {
		goto error_init_sprite_sheet_effects_32;
	}

	if (!sprite_sheet_font_instances_d3d11_init(&program->draw.boxy_bold, device)) {
		goto error_init_sprite_sheet_font_boxy_bold;
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

	if (!debug_draw_world_d3d11_init(&program->debug_draw_world, device)) {
		goto error_init_debug_draw_world;
	}

	program->max_screen_size      = screen_size;
	program->d3d11.output_texture = output_texture;
	program->d3d11.output_uav     = output_uav;
	program->d3d11.output_rtv     = output_rtv;
	program->d3d11.output_srv     = output_srv;
	program->d3d11.output_vs      = output_vs;
	program->d3d11.output_ps      = output_ps;

	return 1;

	debug_draw_world_d3d11_free(&program->debug_draw_world);
error_init_debug_draw_world:
	debug_line_d3d11_free(&program->draw.card_debug_line);
error_init_debug_line:
	card_render_d3d11_free(&program->draw.card_render);
error_init_card_render:
	imgui_d3d11_free(&program->imgui);
error_init_imgui:
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
error_init_pixel_art_upsampler:
	sprite_sheet_font_instances_d3d11_free(&program->draw.boxy_bold);
error_init_sprite_sheet_font_boxy_bold:
	sprite_sheet_instances_d3d11_free(&program->draw.effects_32);
error_init_sprite_sheet_effects_32:
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
	debug_draw_world_d3d11_free(&program->debug_draw_world);
	debug_line_d3d11_free(&program->draw.card_debug_line);
	card_render_d3d11_free(&program->draw.card_render);
	imgui_d3d11_free(&program->imgui);
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
	sprite_sheet_font_instances_d3d11_free(&program->draw.boxy_bold);
	sprite_sheet_instances_d3d11_free(&program->draw.effects_32);
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
	log_reset(debug_log);

	program->debug_draw_world.reset();

	program->screen_size = (v2)screen_size;
	++program->frame_number;
	program->sound.reset();

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
			f32 raw_zoom = (f32)screen_size.y / program->draw.camera.zoom;
			program->draw.camera.world_center -= (v2)input->mouse_delta / raw_zoom;
		} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
		             & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
			program->program_input_state_stack.pop();
		}
		break;
	}
	program->draw.camera.zoom -= (f32)input->mouse_wheel_delta * 0.25f;

	if (input_get_num_up_transitions(input, INPUT_BUTTON_F1) % 2) {
		program->display_debug_ui = !program->display_debug_ui;
	}

	f32 time = (f32)program->frame_number / 60.0f;
	world_anim_draw(&program->world_anim, &program->draw, &program->sound, time);
	program->draw.camera.offset = program->world_anim.camera_offset;

	if (program->program_input_state_stack.peek() == GIS_ANIMATING
	 && !world_anim_is_animating(&program->world_anim)) {
		program->program_input_state_stack.pop();
	}

	v2_i32 world_mouse_pos = (v2_i32)screen_pos_to_world_pos(&program->draw.camera,
	                                                         screen_size,
	                                                         input->mouse_pos);
	u32 sprite_id = sprite_sheet_renderer_id_in_pos(&program->draw.renderer,
	                                                (v2_u32)world_mouse_pos);

	Event_Buffer event_buffer = {};

	// program->card_anim_state.hand_params.screen_width = ratio;
	program->card_anim_state.hand_params.height = constants.cards_ui.height;
	program->card_anim_state.hand_params.border = constants.cards_ui.border;
	program->card_anim_state.hand_params.top    = constants.cards_ui.top;
	program->card_anim_state.hand_params.bottom = constants.cards_ui.bottom;
	// XXX - ugh
	program->card_anim_state.hand_params.card_size = { 0.5f*0.4f*48.0f/80.0f, 0.5f*0.4f*1.0f };

	debug_line_reset(&program->draw.card_debug_line);
	// card_anim_update_anims
	u8 allow_highlight_card = program->program_input_state_stack.peek() == GIS_NONE;
	allow_highlight_card |= program->program_input_state_stack.peek() == GIS_PLAYING_CARDS;
	Card_UI_Event card_event = card_anim_draw(&program->card_anim_state,
	                                          &program->game.card_state,
	                                          &program->draw.card_render,
	                                          &program->sound,
	                                          screen_size,
	                                          time,
	                                          input,
	                                          allow_highlight_card);

	// TODO -- get card id from mouse pos
	v2 card_mouse_pos = (v2)input->mouse_pos / (v2)screen_size;
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

	if (selected_card_id) {
		sprite_id = 0;
	}

	switch (program->program_input_state_stack.peek()) {
	case GIS_ANIMATING:
		sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, 0);
		break;
	case GIS_PLAYING_CARDS: {
		Action player_action = {};
		player_action.type = ACTION_NONE;

		Card_State *card_state = &program->game.card_state;
		card_state->events.reset();
		switch (card_event.type) {
		case CARD_UI_EVENT_HAND_CLICKED: {
			u32 card_id = card_event.hand.card_id;
			u32 hand_index = card_state->hand.len;
			Card *card = NULL;
			for (u32 i = 0; i < card_state->hand.len; ++i) {
				if (card_state->hand[i].id == card_id) {
					card = &card_state->hand[i];
					hand_index = i;
					break;
				}
			}
			ASSERT(card);

			switch (card->appearance) {
			case CARD_APPEARANCE_FIRE_MANA: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_HAND_TO_IN_PLAY;
				card_event.hand_to_in_play.card_id = card_id;
				card_state->events.append(card_event);

				card_state->in_play.append(*card);
				card_state->hand.remove(hand_index);
				break;
			}
			case CARD_APPEARANCE_FIREBALL: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_HAND_TO_IN_PLAY;
				card_event.hand_to_in_play.card_id = card_id;
				card_state->events.append(card_event);

				card_state->in_play.append(*card);
				card_state->hand.remove(hand_index);

				Controller *c = &program->game.controllers.items[0];
				ASSERT(c->type == CONTROLLER_PLAYER);
				Entity *player = game_get_entity_by_id(&program->game,
				                                       c->player.entity_id);
				program->action_being_built.type = ACTION_FIREBALL;
				program->action_being_built.fireball.start = player->pos;

				Card_Param param = {};
				param.type = CARD_PARAM_TARGET;
				param.target.dest = &program->action_being_built.fireball.end;
				program->card_params_stack.push(param);
				program->program_input_state_stack.push(GIS_CARD_PARAMS);

				break;
			}
			case CARD_APPEARANCE_EXCHANGE: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_HAND_TO_IN_PLAY;
				card_event.hand_to_in_play.card_id = card_id;
				card_state->events.append(card_event);

				card_state->in_play.append(*card);
				card_state->hand.remove(hand_index);

				Controller *c = &program->game.controllers.items[0];
				ASSERT(c->type == CONTROLLER_PLAYER);
				Entity *player = game_get_entity_by_id(&program->game,
				                                       c->player.entity_id);
				program->action_being_built.type = ACTION_EXCHANGE;

				Card_Param param = {};
				param.type = CARD_PARAM_CREATURE;
				param.creature.id = &program->action_being_built.exchange.a;
				program->card_params_stack.push(param);
				param.creature.id = &program->action_being_built.exchange.b;
				program->card_params_stack.push(param);
				program->program_input_state_stack.push(GIS_CARD_PARAMS);

				break;
			}
			case CARD_APPEARANCE_BLINK: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_HAND_TO_IN_PLAY;
				card_event.hand_to_in_play.card_id = card_id;
				card_state->events.append(card_event);

				card_state->in_play.append(*card);
				card_state->hand.remove(hand_index);

				Controller *c = &program->game.controllers.items[0];
				ASSERT(c->type == CONTROLLER_PLAYER);
				Entity *player = game_get_entity_by_id(&program->game,
				                                       c->player.entity_id);
				program->action_being_built.type = ACTION_BLINK;
				program->action_being_built.blink.caster = player->id;

				Card_Param param = {};
				param.type = CARD_PARAM_AVAILABLE_TILE;
				param.available_tile.entity_id = player->id;
				param.available_tile.dest = &program->action_being_built.blink.target;
				program->card_params_stack.push(param);
				program->program_input_state_stack.push(GIS_CARD_PARAMS);

				break;
			}
			}

			break;
		}
		}

		if (sprite_id && input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			game_end_play_cards(&program->game);
			Action player_action = {};
			player_action.type = ACTION_WAIT;
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			c->player.action = player_action;
			Event_Buffer event_buffer;
			game_do_turn(&program->game, &event_buffer);
			world_anim_build_events_to_be_animated(&program->world_anim, &event_buffer);
			program->program_input_state_stack.pop();
			program->program_input_state_stack.push(GIS_ANIMATING);
		}

		card_anim_update_anims(&program->card_anim_state, card_state->events, time);

		sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);

		break;
	}
	case GIS_NONE: {
		Action player_action = {};
		player_action.type = ACTION_NONE;

		Card_State *card_state = &program->game.card_state;
		card_state->events.reset();
		switch (card_event.type) {
		case CARD_UI_EVENT_NONE:
			break;
		case CARD_UI_EVENT_DECK_CLICKED: {
			Action_Buffer actions;
			game_draw_cards(&program->game, &actions, 5);
			// program->sound.play(SOUND_CARD_GAME_MOVEMENT_DEAL_SINGLE_01);
			// player_action.type = ACTION_WAIT;
			if (actions) {
				Event_Buffer events;
				events.reset();
				game_simulate_actions(&program->game, actions, &events);
				world_anim_build_events_to_be_animated(&program->world_anim, &events);
				program->program_input_state_stack.push(GIS_ANIMATING);
			}
			program->program_input_state_stack.push(GIS_PLAYING_CARDS);
			break;
		}
		case CARD_UI_EVENT_DISCARD_CLICKED: {
			Action_Buffer actions;
			game_draw_cards(&program->game, &actions, 5);
			// program->sound.play(SOUND_CARD_GAME_MOVEMENT_DEAL_SINGLE_01);
			// player_action.type = ACTION_WAIT;
			if (actions) {
				Event_Buffer events;
				events.reset();
				game_simulate_actions(&program->game, actions, &events);
				world_anim_build_events_to_be_animated(&program->world_anim, &events);
				program->program_input_state_stack.push(GIS_ANIMATING);
			}
			program->program_input_state_stack.push(GIS_PLAYING_CARDS);
			break;
		}
		}

		card_anim_update_anims(&program->card_anim_state, card_state->events, time);

		if (sprite_id && input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			Entity *player = game_get_entity_by_id(&program->game, c->player.entity_id);
			Pos start = player->pos;
			Pos end;
			if (sprite_id < MAX_ENTITIES) {
				Entity *e = game_get_entity_by_id(&program->game, sprite_id);
				end = e->pos;
			} else {
				end = u16_to_pos(sprite_id - MAX_ENTITIES);
			}
			v2 dir = (v2)end - (v2)start;
			dir = dir * dir;
			u8 test = (dir.x || dir.y) && (dir.x <= 1 && dir.y <= 1);
			if (game_is_passable(&program->game, end, player->movement_type) && test) {
				player_action.type = ACTION_MOVE;
				player_action.move.entity_id = player->id;
				player_action.move.start = start;
				player_action.move.end = end;
			}
		}

		if (player_action) {
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			c->player.action = player_action;
			Event_Buffer event_buffer;
			game_do_turn(&program->game, &event_buffer);
			world_anim_build_events_to_be_animated(&program->world_anim, &event_buffer);
			program->program_input_state_stack.push(GIS_ANIMATING);
		}

		sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);
		break;
	}
	case GIS_CARD_PARAMS: {
		Card_Param *param = program->card_params_stack.peek_ptr();
		switch (param->type) {
		case CARD_PARAM_TARGET:
			sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);
			if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
				Pos target;
				if (sprite_id < MAX_ENTITIES) {
					Entity *e = game_get_entity_by_id(&program->game, sprite_id);
					ASSERT(e);
					target = e->pos;
				} else {
					target = u16_to_pos(sprite_id - MAX_ENTITIES);
				}
				*param->target.dest = target;
				program->card_params_stack.pop();
			}
			break;
		case CARD_PARAM_CREATURE: {
			Entity *e = game_get_entity_by_id(&program->game, sprite_id);
			if (!e) {
				sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, 0);
				break;
			}
			sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);
			if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
				*param->creature.id = e->id;
				program->card_params_stack.pop();
			}
			break;
		}
		case CARD_PARAM_AVAILABLE_TILE: {
			sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, 0);
			Pos target;
			if (sprite_id < MAX_ENTITIES) {
				Entity *e = game_get_entity_by_id(&program->game, sprite_id);
				if (!e) {
					break;
				}
				target = e->pos;
			} else {
				target = u16_to_pos(sprite_id - MAX_ENTITIES);
			}
			Entity *mover_e = game_get_entity_by_id(&program->game,
			                                        param->available_tile.entity_id);
			ASSERT(mover_e);
			if (!game_is_passable(&program->game, target, mover_e->movement_type)) {
				break;
			}
			sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);
			if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
				*param->available_tile.dest = target;
				program->card_params_stack.pop();
			}
			break;
		}
		}

		if (!program->card_params_stack) {
			// TODO -- cast spell
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			c->player.action = program->action_being_built;
			Event_Buffer event_buffer;
			// game_do_turn(&program->game, &event_buffer);
			game_simulate_actions(&program->game,
			                      slice_one(&program->action_being_built),
			                      &event_buffer);
			world_anim_build_events_to_be_animated(&program->world_anim, &event_buffer);
			// c->player.action.type = ACTION_WAIT;
			// game_do_turn(&program->game, &event_buffer);
			// world_anim_build_events_to_be_animated(&program->world_anim, &event_buffer);
			program->program_input_state_stack.pop();
			program->program_input_state_stack.push(GIS_ANIMATING);
		}
		break;
	}
	case GIS_DRAGGING_MAP:
		sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, 0);
		break;
	}

	// do sprite tooltip
	if (sprite_id && sprite_id < MAX_ENTITIES) {
		Entity *e = game_get_entity_by_id(&program->game, sprite_id);
		if (e) {
			char buffer[1024];
			snprintf(buffer, ARRAY_SIZE(buffer), "%d/%d", e->hit_points, e->max_hit_points);

			u32 width = 1, height = 0;
			for (u8 *p = (u8*)buffer; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				width += glyph.dimensions.w - 1;
				height = max(height, glyph.dimensions.h);
			}

			Sprite_Sheet_Font_Instance instance = {};
			instance.world_pos = (v2)e->pos + V2_f32(0.5f, -0.25f);
			instance.world_offset = { -(f32)(width / 2), -(f32)(height / 2) };
			instance.zoom = 1.0f;
			instance.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			for (u8 *p = (u8*)buffer; *p; ++p) {
				Boxy_Bold_Glyph_Pos glyph = boxy_bold_glyph_poss[*p];
				instance.glyph_pos = (v2)glyph.top_left;
				instance.glyph_size = (v2)glyph.dimensions;

				sprite_sheet_font_instances_add(&program->draw.boxy_bold, instance);

				instance.world_offset.x += (f32)glyph.dimensions.w - 1.0f;
			}
		}
	}

	// imgui
	imgui_begin(&program->imgui, input, screen_size);
	if (program->display_debug_ui) {
		imgui_set_text_cursor(&program->imgui, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
		if (imgui_tree_begin(&program->imgui, "show mouse info")) {
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
			Pos sprite_pos;
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
			snprintf(buffer, ARRAY_SIZE(buffer), "Sprite ID: %u, position: (%u, %u)",
			         sprite_id, sprite_pos.x, sprite_pos.y);
			imgui_text(&program->imgui, buffer);
			snprintf(buffer, ARRAY_SIZE(buffer), "Card mouse pos: (%f, %f)",
				 card_mouse_pos.x, card_mouse_pos.y);
			imgui_text(&program->imgui, buffer);
			snprintf(buffer, ARRAY_SIZE(buffer), "Selected Card ID: %u", selected_card_id);
			imgui_text(&program->imgui, buffer);
			snprintf(buffer, ARRAY_SIZE(buffer), "World Center: (%f, %f)",
				program->draw.camera.world_center.x,
				program->draw.camera.world_center.y);
			imgui_text(&program->imgui, buffer);
			// draw input state stack
			{
				imgui_text(&program->imgui, "Program Input State Stack:");
				auto& state_stack = program->program_input_state_stack;
				for (u32 i = 0; i < state_stack.top; ++i) {
					snprintf(buffer, ARRAY_SIZE(buffer), "    State: %s",
					         PROGRAM_INPUT_STATE_NAMES[state_stack.items[i]]);
					imgui_text(&program->imgui, buffer);
				}
			}
			imgui_tree_end(&program->imgui);
		}
		if (imgui_tree_begin(&program->imgui, "levels")) {
			if (imgui_button(&program->imgui, "default")) {
				build_level_default(program);
			}
			if (imgui_button(&program->imgui, "anim test")) {
				build_level_anim_test(program);
			}
			if (imgui_button(&program->imgui, "slime test")) {
				build_level_slime_test(program);
			}
			imgui_tree_end(&program->imgui);
		}
		if (imgui_tree_begin(&program->imgui, "cards")) {
			if (imgui_button(&program->imgui, "random 100")) {
				build_deck_random_100(program);
			}
			if (imgui_button(&program->imgui, "poison")) {
				build_deck_poison(program);
			}
			imgui_tree_end(&program->imgui);
		}
		constants_do_imgui(&program->imgui);
		if (imgui_tree_begin(&program->imgui, "debug log")) {
			u32 start = 0;
			u32 end = debug_log->cur_line;
			if (end > LOG_MAX_LINES) {
				start = end - LOG_MAX_LINES;
			}

			for (u32 i = start; i < end; ++i) {
				imgui_text(&program->imgui, log_get_line(debug_log, i));
			}
			imgui_tree_end(&program->imgui);
		}
		imgui_tree_end(&program->imgui);
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
	ASSERT(!interlocked_compare_exchange(&program->process_frame_signal, 1, 0));
}

void process_frame(Program* program, Input* input, v2_u32 screen_size)
{
	set_global_state(program);

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
		switch (program->program_input_state_stack.peek()) {
		case GIS_NONE:
			if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_ENDED_DOWN) {
				program->program_input_state_stack.push(GIS_DRAGGING_MAP);
			}
			break;
		case GIS_DRAGGING_MAP:
			if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
			  & INPUT_BUTTON_FLAG_HELD_DOWN) {
				f32 raw_zoom = (f32)screen_size.y / program->draw.camera.zoom;
				program->draw.camera.world_center -= (v2)input->mouse_delta / raw_zoom;
			} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
				     & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
				program->program_input_state_stack.pop();
			}
			break;
		}
		program->draw.camera.zoom -= (f32)input->mouse_wheel_delta * 0.25f;
		if (input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			program->state = PROGRAM_STATE_NORMAL;
			interlocked_compare_exchange(&program->debug_resume, 1, 0);
			while (!interlocked_compare_exchange(&program->process_frame_signal, 0, 1)) {
				sleep(0);
			}
		} else {
			imgui_begin(&program->imgui, input, screen_size);
			imgui_set_text_cursor(&program->imgui,
			                      { 1.0f, 0.0f, 1.0f, 1.0f },
			                      { 5.0f, 5.0f });
			imgui_text(&program->imgui, "DEBUG PAUSE");
		}
		break;
	}
}

void render_d3d11(Program* program, ID3D11DeviceContext* dc, ID3D11RenderTargetView* output_rtv)
{
	v2_u32 screen_size_u32 = (v2_u32)program->screen_size;

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(program->d3d11.output_rtv, clear_color);

	sprite_sheet_renderer_d3d11_begin(&program->draw.renderer, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.tiles, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.creatures, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.water_edges, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.effects_32, dc, screen_size_u32);
	sprite_sheet_renderer_d3d11_highlight_sprite(&program->draw.renderer, dc);
	sprite_sheet_renderer_d3d11_begin_font(&program->draw.renderer, dc);
	sprite_sheet_font_instances_d3d11_draw(&program->draw.boxy_bold, dc, screen_size_u32);
	sprite_sheet_renderer_d3d11_end(&program->draw.renderer, dc);

	f32 zoom = program->draw.camera.zoom;
	v2 world_tl = screen_pos_to_world_pos(&program->draw.camera,
	                                      screen_size_u32,
	                                      { 0, 0 });
	v2 world_br = screen_pos_to_world_pos(&program->draw.camera,
	                                      screen_size_u32,
	                                      screen_size_u32);
	v2 input_size = world_br - world_tl;
	pixel_art_upsampler_d3d11_draw(&program->pixel_art_upsampler,
	                               dc,
	                               program->draw.renderer.d3d11.output_srv,
	                               program->d3d11.output_uav,
	                               input_size,
	                               world_tl,
	                               (v2)screen_size_u32,
	                               { 0.0f, 0.0f });

	card_render_d3d11_draw(&program->draw.card_render,
	                       dc,
	                       screen_size_u32,
	                       program->d3d11.output_rtv);
	// debug_line_d3d11_draw(&program->draw.card_debug_line, dc, program->d3d11.output_rtv);

	v2 debug_zoom = (v2)(world_br - world_tl) / 24.0f;
	debug_draw_world_d3d11_draw(&program->debug_draw_world,
	                            dc,
	                            output_rtv,
	                            program->draw.camera.world_center + program->draw.camera.offset,
	                            debug_zoom);

	imgui_d3d11_draw(&program->imgui, dc, program->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(program->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(program->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &program->d3d11.output_srv);
	dc->Draw(6, 0);
}
