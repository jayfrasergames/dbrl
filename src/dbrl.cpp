#include "dbrl.h"

#include "imgui.h"
#include "jfg_math.h"
#include "log.h"
#include "debug_line_draw.h"
#include "containers.hpp"
#include "random.h"
#include "thread.h"

#include "types.h"

#include "field_of_vision_render.h"
#include "assets.h"
#include "constants.h"
#include "debug_draw_world.h"
#include "sound.h"
#include "sprite_sheet.h"
#include "card_render.h"
#include "pixel_art_upsampler.h"
#include "particles.h"
#include "physics.h"

#include <stdio.h>  // XXX - for snprintf

#include "gen/cards.data.h"
#include "gen/appearance.data.h"
#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"
#include "gen/sprite_sheet_water_edges.data.h"
#include "gen/sprite_sheet_effects_24.data.h"
#include "gen/sprite_sheet_effects_32.data.h"
#include "gen/boxy_bold.data.h"

#include "gen/pass_through_dxbc_vertex_shader.data.h"
#include "gen/pass_through_dxbc_pixel_shader.data.h"

#include "draw.h"

// =============================================================================
// Global Platform Functions

#define PLATFORM_FUNCTION(return_type, name, ...) return_type (*name)(__VA_ARGS__) = NULL;
PLATFORM_FUNCTIONS
#undef PLATFORM_FUNCTION

// =============================================================================
// type definitions/constants


struct Program;
thread_local Program *global_program;
thread_local Log *debug_log;
thread_local Log *debug_pause_log;
void debug_pause();

#define DLOG(...) log(debug_log, __VA_ARGS__)
#define DPLOG(...) log(debug_pause_log, __VA_ARGS__)

template <typename T>
struct Map_Cache
{
	T items[256 * 256];
	T& operator[](Pos pos) { return items[pos.y * 256 + pos.x]; }
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
	ACTION_FIRE_BOLT,
	ACTION_EXCHANGE,
	ACTION_BLINK,
	ACTION_POISON,
	ACTION_HEAL,
	ACTION_LIGHTNING,
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
		struct {
			Entity_ID caster_id;
			Entity_ID target_id;
			// Pos start;
			// Pos end;
		} fire_bolt;
		struct {
			Entity_ID caster_id;
			Entity_ID target_id;
			i32 amount;
		} heal;
		struct {
			Entity_ID caster_id;
			Pos start;
			Pos end;
		} lightning;
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
	EVENT_FIREBALL_OFFSHOOT_2,
	EVENT_STUCK,
	EVENT_DAMAGED,
	EVENT_DEATH,
	EVENT_EXCHANGE,
	EVENT_BLINK,
	EVENT_SLIME_SPLIT,
	EVENT_FIRE_BOLT_SHOT,
	EVENT_POLYMORPH,
	EVENT_HEAL,
	EVENT_FIELD_OF_VISION_CHANGED,
	EVENT_LIGHTNING_BOLT,
	EVENT_LIGHTNING_BOLT_START,
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
			v2 start;
			v2 end;
		} fireball_offshoot_2;
		struct {
			Entity_ID entity_id;
			v2 pos;
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
			Pos       start;
			Pos       target;
		} blink;
		struct {
			Entity_ID original_id;
			Entity_ID new_id;
			v2 start;
			v2 end;
		} slime_split;
		struct {
			v2 start;
			v2 end;
			f32 duration;
		} fire_bolt_shot;
		struct {
			Entity_ID  entity_id;
			Appearance new_appearance;
			Pos        pos;
		} polymorph;
		struct {
			Entity_ID caster_id;
			Entity_ID target_id;
			i32 amount;
			v2 start;
			v2 end;
		} heal;
		struct {
			f32 duration;
			Map_Cache_Bool *fov;
		} field_of_vision;
		struct {
			Entity_ID caster_id;
			f32 duration;
			v2 start;
			v2 end;
		} lightning;
		struct {
			f32 duration;
			v2 start;
			v2 end;
		} lightning_bolt;
		struct {
			v2 pos;
		} lightning_bolt_start;
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
	case EVENT_FIREBALL_OFFSHOOT_2: return 0;
	case EVENT_STUCK: return 0;
	case EVENT_DAMAGED: return 0;
	case EVENT_DEATH: return 0;
	case EVENT_EXCHANGE: return 0;
	case EVENT_BLINK: return 0;
	case EVENT_SLIME_SPLIT: return 0;
	case EVENT_FIRE_BOLT_SHOT: return 0;
	case EVENT_POLYMORPH: return 0;
	case EVENT_HEAL: return 0;
	case EVENT_FIELD_OF_VISION_CHANGED: return 0;
	case EVENT_LIGHTNING_BOLT: return 0;
	case EVENT_LIGHTNING_BOLT_START: return 0;
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
	u8            blocks_vision;
};

enum Controller_Type
{
	CONTROLLER_PLAYER,
	CONTROLLER_RANDOM_MOVE,
	CONTROLLER_DRAGON,
	CONTROLLER_SLIME,
	CONTROLLER_LICH,

	NUM_CONTROLLERS,
};

#define CONTROLLER_LICH_MAX_SKELETONS 16

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
			Entity_ID entity_id;
		} dragon;
		struct {
			Entity_ID entity_id;
			u32       split_cooldown;
		} slime;
		struct {
			Entity_ID lich_id;
			Max_Length_Array<Entity_ID, CONTROLLER_LICH_MAX_SKELETONS> skeleton_ids;
			u32 heal_cooldown;
		} lich;
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
	CARD_EVENT_SELECT,
	CARD_EVENT_UNSELECT,
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
		struct {
			u32 card_id;
		} select;
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
	MESSAGE_PRE_DEATH       = 1 << 5,
	MESSAGE_POST_DEATH      = 1 << 6,
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
		struct {
			Entity_ID entity_id;
		} death;
	};
};

enum Message_Handler_Type
{
	MESSAGE_HANDLER_PREVENT_EXIT,
	MESSAGE_HANDLER_PREVENT_ENTER,
	MESSAGE_HANDLER_DROP_TILE,
	MESSAGE_HANDLER_TRAP_FIREBALL,
	MESSAGE_HANDLER_SLIME_SPLIT,
	MESSAGE_HANDLER_LICH_DEATH,
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
		struct {
			Controller_ID controller_id;
		} lich_death;
	};
};

#define GAME_MAX_CONTROLLERS 1024
#define GAME_MAX_MESSAGE_HANDLERS 1024

enum Static_Entity_ID
{
	ENTITY_ID_NONE,
	ENTITY_ID_PLAYER,
	ENTITY_ID_WALLS,
	NUM_STATIC_ENTITY_IDS,
};

// maybe better called "world state"?
struct Game
{
	Entity_ID     next_entity_id;
	Controller_ID next_controller_id;

	// TODO: keep the player id here in the game struct? - it would make life a lot easier
	// Entity_ID player_id;

	// probably don't need this
	u32 cur_block_id;
	f32 block_time;

	Max_Length_Array<Entity, MAX_ENTITIES> entities;

	Map_Cache<Tile> tiles;

	Max_Length_Array<Controller, GAME_MAX_CONTROLLERS> controllers;

	Card_State card_state;

	Max_Length_Array<Message_Handler, GAME_MAX_MESSAGE_HANDLERS> handlers;

	Map_Cache_Bool field_of_vision;
};

Entity_ID game_new_entity_id(Game *game)
{
	return game->next_entity_id++;
}

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
			break;
		case CONTROLLER_LICH: {
			// XXX - not sure if this should remove the controller
			if (c->lich.lich_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			auto& skeleton_ids = c->lich.skeleton_ids;
			u32 num_skeleton_ids = skeleton_ids.len;
			for (u32 i = 0; i < num_skeleton_ids; ++i) {
				if (skeleton_ids[i] == entity_id) {
					skeleton_ids.remove(i);
					goto break_loop;
				}
			}
			break;
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

u8 game_is_pos_opaque(Game* game, Pos pos)
{
	auto& tiles = game->tiles;
	switch (tiles[pos].type) {
	case TILE_EMPTY:
	case TILE_FLOOR:
	case TILE_WATER:
		return 0;
		break;
	case TILE_WALL:
		return 1;
		break;
	default:
		ASSERT(0);
	}
	return 0;
}

void calculate_field_of_vision(Game *game, Entity_ID vision_entity_id, Map_Cache_Bool *fov)
{
	const u8 is_wall            = 1 << 0;
	const u8 bevel_top_left     = 1 << 1;
	const u8 bevel_top_right    = 1 << 2;
	const u8 bevel_bottom_left  = 1 << 3;
	const u8 bevel_bottom_right = 1 << 4;
	Map_Cache<u8> map;
	memset(map.items, 0, sizeof(map.items));

	auto& tiles = game->tiles;
	for (u16 y = 0; y < 256; ++y) {
		for (u16 x = 0; x < 256; ++x) {
			Pos p = Pos((u8)x, (u8)y);
			if (game_is_pos_opaque(game, p)) {
				map[p] |= is_wall;
			}
		}
	}

	for (u16 i = 0; i < 256; ++i) {
		u8 x = (u8)i;
		map[Pos(  x,   0)] |= is_wall;
		map[Pos(  x, 255)] |= is_wall;
		map[Pos(  0,   x)] |= is_wall;
		map[Pos(255,   x)] |= is_wall;
	}

	auto& entities = game->entities;
	for (u32 i = 0; i < entities.len; ++i) {
		if (entities[i].blocks_vision) {
			map[entities[i].pos] |= is_wall;
		}
	}

	for (u8 y = 1; y < 255; ++y) {
		for (u8 x = 1; x < 255; ++x) {
			u8 center = map[Pos(x, y)];
			if (!center) {
				continue;
			}

			u8 above  = map[Pos(    x, y - 1)];
			u8 left   = map[Pos(x - 1,     y)];
			u8 right  = map[Pos(x + 1,     y)];
			u8 bottom = map[Pos(    x, y + 1)];

			if (!above  && !left)  { center |= bevel_top_left;     }
			if (!above  && !right) { center |= bevel_top_right;    }
			if (!bottom && !left)  { center |= bevel_bottom_left;  }
			if (!bottom && !right) { center |= bevel_bottom_right; }

			map[Pos(x, y)] = center;
		}
	}

	Entity *vision_entity = game_get_entity_by_id(game, vision_entity_id);
	ASSERT(vision_entity);

	struct Sector
	{
		Rational start;
		Rational end;
	};

	fov->reset();
	Max_Length_Array<Sector, 1024> sectors_1, sectors_2;
	Max_Length_Array<Sector, 1024> *sectors_front = &sectors_1, *sectors_back = &sectors_2;

	const i32 half_cell_size = 2;
	const i32 cell_margin = 1;
	const i32 cell_size = 2 * half_cell_size;
	const i32 cell_inner_size = cell_size - 2 * cell_margin;
	const Rational wall_see_low  = Rational::cancel(1, 6);
	const Rational wall_see_high = Rational::cancel(5, 6);

	Pos vision_pos = vision_entity->pos;
	fov->set(vision_pos);

	for (u8 octant_id = 0; octant_id < 8; ++octant_id) {
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

		sectors_front->reset();
		sectors_back->reset();
		{
			Sector s = {};
			s.start.numerator = 0;
			s.start.denominator = 1;
			s.end.numerator = 1;
			s.end.denominator = 1;
			sectors_front->append(s);
		}

		for (u16 y_iter = 0; ; ++y_iter) {
			auto& old_sectors = *sectors_front;
			auto& new_sectors = *sectors_back;
			new_sectors.reset();

			if (!old_sectors) {
				goto next_octant;
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
					u8 btl, bbr;
					i32 x, y;
					switch (octant_id) {
					case 0:
						btl = bevel_top_left;
						bbr = bevel_bottom_right;
						x = (i32)vision_pos.x + (i32)x_iter;
						y = (i32)vision_pos.y - (i32)y_iter;
						break;
					case 1:
						btl = bevel_bottom_right;
						bbr = bevel_top_left;
						x = (i32)vision_pos.x + (i32)y_iter;
						y = (i32)vision_pos.y - (i32)x_iter;
						break;
					case 2:
						btl = bevel_bottom_left;
						bbr = bevel_top_right;
						x = (i32)vision_pos.x + (i32)x_iter;
						y = (i32)vision_pos.y + (i32)y_iter;
						break;
					case 3:
						btl = bevel_top_right;
						bbr = bevel_bottom_left;
						x = (i32)vision_pos.x + (i32)y_iter;
						y = (i32)vision_pos.y + (i32)x_iter;
						break;
					case 4:
						btl = bevel_top_right;
						bbr = bevel_bottom_left;
						x = (i32)vision_pos.x - (i32)x_iter;
						y = (i32)vision_pos.y - (i32)y_iter;
						break;
					case 5:
						btl = bevel_bottom_left;
						bbr = bevel_top_right;
						x = (i32)vision_pos.x - (i32)y_iter;
						y = (i32)vision_pos.y - (i32)x_iter;
						break;
					case 6:
						btl = bevel_bottom_right;
						bbr = bevel_top_left;
						x = (i32)vision_pos.x - (i32)x_iter;
						y = (i32)vision_pos.y + (i32)y_iter;
						break;
					case 7:
						btl = bevel_top_left;
						bbr = bevel_bottom_right;
						x = (i32)vision_pos.x - (i32)y_iter;
						y = (i32)vision_pos.y + (i32)x_iter;
						break;
					}
					if (!(0 < y && y < 255)) { goto next_octant; }
					if (!(0 < x && x < 255)) { x_end = x_iter; }
					Pos p = Pos((u8)x, (u8)y);
					u8 cell = map[p];
					if (cell & is_wall) {
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

						if (horiz_left_intersect  <= wall_see_high
						 && horiz_right_intersect >= wall_see_low) {
							fov->set(p);
						} else if (prev_was_clear
							&& vert_left_intersect  >= wall_see_low
							&& vert_right_intersect <= wall_see_high) {
							fov->set(p);
						}

						Rational left_slope, right_slope;
						if (cell & btl) {
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
						if (cell & bbr) {
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
							fov->set(p);
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

	next_octant: ;
	}
}

u8 calculate_line_of_sight(Game *game, Entity_ID vision_entity_id, Pos target)
{
	Entity *e = game_get_entity_by_id(game, vision_entity_id);
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
					u8 top    = game_is_pos_opaque(game, (v2_u8)(cur_pos + up));
					u8 bottom = game_is_pos_opaque(game, (v2_u8)(cur_pos - up));
					u8 left_  = game_is_pos_opaque(game, (v2_u8)(cur_pos + left));
					u8 right  = game_is_pos_opaque(game, (v2_u8)(cur_pos - left));
					u8 btl = !top    && !left_;
					u8 bbr = !bottom && !right;
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

Entity_ID lich_get_skeleton_to_heal(Game *game, Controller *controller)
{
	Entity_ID best_skeleton_id = 0;
	if (!controller->lich.heal_cooldown) {
		auto& skeleton_ids = controller->lich.skeleton_ids;
		u32 num_skeleton_ids = skeleton_ids.len;
		i32 best_heal = 0;
		for (u32 i = 0; i < num_skeleton_ids; ++i) {
			Entity_ID e_id = skeleton_ids[i];
			Entity *e = game_get_entity_by_id(game, e_id);
			ASSERT(e);
			i32 heal = e->max_hit_points - e->hit_points;
			if (heal > best_heal) {
				best_skeleton_id = e_id;
				best_heal = heal;
			}
		}
	}
	return best_skeleton_id;
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

	auto& hand = card_state->hand;
	for (u32 i = 0; i < hand.len; ++i) {
		Card card = hand[i];
		card_state->discard.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_HAND_TO_DISCARD;
		event.hand_to_discard.card_id = card.id;
		card_state->events.append(event);
	}
	hand.len = 0;
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
	TRANSACTION_FIREBALL_HIT,
	TRANSACTION_EXCHANGE_CAST,
	TRANSACTION_EXCHANGE,
	TRANSACTION_BLINK_CAST,
	TRANSACTION_BLINK,
	TRANSACTION_POISON_CAST,
	TRANSACTION_POISON,
	TRANSACTION_SLIME_SPLIT,
	TRANSACTION_FIRE_BOLT_CAST,
	TRANSACTION_FIRE_BOLT_SHOT,
	TRANSACTION_FIRE_BOLT_HIT,
	TRANSACTION_HEAL_CAST,
	TRANSACTION_HEAL,
	TRANSACTION_LIGHTNING_CAST,
	TRANSACTION_LIGHTNING_SHOT,
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
		struct {
			Entity_ID caster_id;
			Entity_ID target_id;
			Pos start;
			Pos end;
		} fire_bolt;
		struct {
			Entity_ID caster_id;
			Entity_ID target_id;
			i32 amount;
			Pos start;
		} heal;
		struct {
			Entity_ID caster_id;
			Pos start;
			Pos end;
		} lightning;
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
		Message_Handler *h = &handlers[i];
		if (!(message.type & h->handle_mask)) {
			continue;
		}
		switch (h->type) {
		case MESSAGE_HANDLER_PREVENT_EXIT: {
			if (h->prevent_exit.pos == message.move.start) {
				u8 *can_exit = (u8*)data;
				*can_exit = 0;
			}
			break;
		}
		case MESSAGE_HANDLER_PREVENT_ENTER: {
			if (h->prevent_enter.pos.x == message.move.end.x
			 && h->prevent_enter.pos.y == message.move.end.y) {
				u8 *can_enter = (u8*)data;
				*can_enter = 0;
			}
			break;
		}
		case MESSAGE_HANDLER_DROP_TILE:
			if (h->trap.pos == message.move.start) {
				// game->tiles[h.trap.pos].type = TILE_EMPTY;
				Transaction t = {};
				t.type = TRANSACTION_DROP_TILE;
				t.start_time = time + 0.05f;
				t.drop_tile.pos = h->trap.pos;
				transactions.append(t);
			}
			break;
		case MESSAGE_HANDLER_TRAP_FIREBALL:
			if (h->trap.pos == message.move.end) {
				Transaction t = {};
				t.type = TRANSACTION_FIREBALL_HIT;
				t.start_time = time;
				t.fireball_shot.end = message.move.end;
				transactions.append(t);

				Event e = {};
				e.type = EVENT_FIREBALL_HIT;
				e.time = time;
				e.fireball_hit.pos = h->trap.pos;
				events.append(e);
			}
			break;
		case MESSAGE_HANDLER_SLIME_SPLIT:
			if (h->owner_id == message.damage.entity_id && !message.damage.entity_died) {
				Entity *e = game_get_entity_by_id(game, h->owner_id);
				Pos p = e->pos;
				/*
				debug_draw_world_reset();
				debug_draw_world_set_color(v4(0.0f, 1.0f, 0.0f, 1.0f));
				debug_draw_world_circle((v2)p, 0.25f);
				debug_pause();
				*/
				Transaction t = {};
				t.type = TRANSACTION_SLIME_SPLIT;
				t.start_time = time + TRANSACTION_EPSILON;
				t.slime_split.slime_id = h->owner_id;
				t.slime_split.hit_points = e->hit_points;
				transactions.append(t);
			}
			break;
		case MESSAGE_HANDLER_LICH_DEATH:
			if (h->owner_id == message.death.entity_id) {
				Controller_ID c_id = h->lich_death.controller_id;
				auto& controllers = game->controllers;
				u32 num_controllers = game->controllers.len;
				Controller *c = NULL;
				for (u32 i = 0; i < num_controllers; ++i) {
					if (controllers[i].id == c_id) {
						c = &controllers[i];
						break;
					}
				}
				ASSERT(c);
				auto& skeleton_ids = c->lich.skeleton_ids;
				if (!skeleton_ids) {
					break;
				}
				u32 idx = rand_u32() % skeleton_ids.len;
				Entity_ID new_lich_id = skeleton_ids[idx];
				skeleton_ids.remove(idx);
				h->owner_id = new_lich_id;
				c->lich.lich_id = new_lich_id;

				Entity *entity = game_get_entity_by_id(game, new_lich_id);
				ASSERT(entity);

				Event e = {};
				e.type = EVENT_POLYMORPH;
				e.time = time;
				e.polymorph.entity_id = new_lich_id;
				e.polymorph.new_appearance = APPEARANCE_CREATURE_NECROMANCER;
				e.polymorph.pos = entity->pos;
				events.append(e);
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
	// TODO -- field of vision

	// =====================================================================
	// simulate actions

	u32 num_entities = game->entities.len;
	Entity *entities = game->entities.items;
	auto& tiles = game->tiles;

	// 0. init physics context

	Physics_Context physics;
	physics_reset(&physics);

	const u32 collision_mask_wall       = 1 << 0;
	const u32 collision_mask_entity     = 1 << 1;
	const u32 collision_mask_projectile = 1 << 2;

	const u32 collides_with_wall       = collision_mask_projectile;
	const u32 collides_with_entity     = collision_mask_projectile;
	const u32 collides_with_projectile = collision_mask_wall | collision_mask_entity;

	// make static lines
	{
		const u8 is_wall            = 1 << 0;
		const u8 bevel_top_left     = 1 << 1;
		const u8 bevel_top_right    = 1 << 2;
		const u8 bevel_bottom_left  = 1 << 3;
		const u8 bevel_bottom_right = 1 << 4;
		Map_Cache<u8> map;
		memset(map.items, 0, sizeof(map.items));

		auto& tiles = game->tiles;
		for (u16 y = 0; y < 256; ++y) {
			for (u16 x = 0; x < 256; ++x) {
				Pos p = Pos((u8)x, (u8)y);
				if (game_is_pos_opaque(game, p)) {
					map[p] |= is_wall;
				}
			}
		}

		for (u16 i = 0; i < 256; ++i) {
			u8 x = (u8)i;
			map[Pos(  x,   0)] |= is_wall;
			map[Pos(  x, 255)] |= is_wall;
			map[Pos(  0,   x)] |= is_wall;
			map[Pos(255,   x)] |= is_wall;
		}

		auto& entities = game->entities;
		for (u32 i = 0; i < entities.len; ++i) {
			if (entities[i].blocks_vision) {
				map[entities[i].pos] |= is_wall;
			}
		}

		for (u8 y = 1; y < 255; ++y) {
			for (u8 x = 1; x < 255; ++x) {
				u8 center = map[Pos(x, y)];
				if (!center) {
					continue;
				}

				u8 above  = map[Pos(    x, y - 1)];
				u8 left   = map[Pos(x - 1,     y)];
				u8 right  = map[Pos(x + 1,     y)];
				u8 bottom = map[Pos(    x, y + 1)];

				if (!above  && !left)  { center |= bevel_top_left;     }
				if (!above  && !right) { center |= bevel_top_right;    }
				if (!bottom && !left)  { center |= bevel_bottom_left;  }
				if (!bottom && !right) { center |= bevel_bottom_right; }

				map[Pos(x, y)] = center;
			}
		}

		Physics_Static_Line line = {};
		line.owner_id = ENTITY_ID_WALLS;
		line.collision_mask = collision_mask_wall;
		line.collides_with_mask = collides_with_wall;
		for (u16 y = 1; y < 255; ++y) {
			for (u16 x = 1; x < 255; ++x) {
				v2 p = v2((f32)x, (f32)y);
				u8 cell = map[(v2_u8)p];
				if (cell & bevel_top_left) {
					line.start = p - v2(0.0f, 0.5f);
					line.end = p - v2(0.5f, 0.0f);
					physics.static_lines.append(line);
				}
				if (cell & bevel_top_right) {
					line.start = p - v2(0.0f, 0.5f);
					line.end = p + v2(0.5f, 0.0f);
					physics.static_lines.append(line);
				}
				if (cell & bevel_bottom_left) {
					line.start = p + v2(0.0f, 0.5f);
					line.end = p - v2(0.5f, 0.0f);
					physics.static_lines.append(line);
				}
				if (cell & bevel_bottom_right) {
					line.start = p + v2(0.0f, 0.5f);
					line.end = p + v2(0.5f, 0.0f);
					physics.static_lines.append(line);
				}
			}
		}

		for (u16 y = 0; y < 255; ++y) {
			// 0 - not drawing line, 1 - line for cell above, 2 - line for cell below
			u8 drawing_line = 0;
			f32 line_start_x, line_end_x;
			for (u16 x = 0; x < 256; ++x) {
				Pos p_above = Pos((u8)x,       (u8)y);
				Pos p_below = Pos((u8)x, (u8)(y + 1));
				u8 cell_above = map[p_above];
				u8 cell_below = map[p_below];
				f32 this_line_start_x = (f32)x - 0.5f, this_line_end_x = (f32)x + 0.5f;
				if ((cell_above & bevel_bottom_left) || (cell_below & bevel_top_left)) {
					this_line_start_x = (f32)x;
				}
				if ((cell_above & bevel_bottom_right) || (cell_below & bevel_top_right)) {
					this_line_end_x = (f32)x;
				}
				if ((cell_above & is_wall) != (cell_below & is_wall)) {
					if (cell_above & is_wall) {
						switch (drawing_line) {
						case 0:
							line_start_x = this_line_start_x;
							line_end_x = this_line_end_x;
							drawing_line = 1;
							break;
						case 1:
							line_end_x = this_line_end_x;
							break;
						case 2:
							line.start = v2(line_start_x, (f32)y + 0.5f);
							line.end   = v2(  line_end_x, (f32)y + 0.5f);
							if (line.start != line.end) {
								physics.static_lines.append(line);
							}
							line_start_x = this_line_start_x;
							line_end_x = this_line_end_x;
							drawing_line = 1;
							break;
						default:
							ASSERT(0);
							break;
						}
					} else {
						switch (drawing_line) {
						case 0:
							line_start_x = this_line_start_x;
							line_end_x = this_line_end_x;
							drawing_line = 2;
							break;
						case 1:
							line.start = v2(line_start_x, (f32)y + 0.5f);
							line.end   = v2(  line_end_x, (f32)y + 0.5f);
							if (line.start != line.end) {
								physics.static_lines.append(line);
							}
							line_start_x = this_line_start_x;
							line_end_x = this_line_end_x;
							drawing_line = 2;
							break;
						case 2:
							line_end_x = this_line_end_x;
							break;
						default:
							ASSERT(0);
							break;
						}
					}
				} else {
					if (drawing_line) {
						line.start = v2(line_start_x, (f32)y + 0.5f);
						line.end   = v2(  line_end_x, (f32)y + 0.5f);
						if (line.start != line.end) {
							physics.static_lines.append(line);
						}
						drawing_line = 0;
					}
				}
			}
		}

		for (u16 x = 0; x < 255; ++x) {
			// 0 - not drawing line, 1 - line for left cell, 2 - line for right cell
			u8 drawing_line = 0;
			f32 line_start_y, line_end_y;
			for (u16 y = 0; y < 256; ++y) {
				Pos p_left  = Pos(      (u8)x, (u8)y);
				Pos p_right = Pos((u8)(x + 1), (u8)y);
				u8 cell_left  = map[p_left];
				u8 cell_right = map[p_right];
				f32 this_line_start_y = (f32)y - 0.5f, this_line_end_y = (f32)y + 0.5f;
				if ((cell_left & bevel_top_right) || (cell_right & bevel_top_left)) {
					this_line_start_y = (f32)y;
				}
				if ((cell_left & bevel_bottom_right) || (cell_right & bevel_bottom_left)) {
					this_line_end_y = (f32)y;
				}
				if ((cell_left & is_wall) != (cell_right & is_wall)) {
					if (cell_left & is_wall) {
						switch (drawing_line) {
						case 0:
							line_start_y = this_line_start_y;
							line_end_y = this_line_end_y;
							drawing_line = 1;
							break;
						case 1:
							line_end_y = this_line_end_y;
							break;
						case 2:
							line.start = v2((f32)x + 0.5f, line_start_y);
							line.end   = v2((f32)x + 0.5f,   line_end_y);
							if (line.start != line.end) {
								physics.static_lines.append(line);
							}
							line_start_y = this_line_start_y;
							line_end_y = this_line_end_y;
							drawing_line = 1;
							break;
						default:
							ASSERT(0);
							break;
						}
					} else {
						switch (drawing_line) {
						case 0:
							line_start_y = this_line_start_y;
							line_end_y = this_line_end_y;
							drawing_line = 2;
							break;
						case 1:
							line.start = v2((f32)x + 0.5f, line_start_y);
							line.end   = v2((f32)x + 0.5f,   line_end_y);
							if (line.start != line.end) {
								physics.static_lines.append(line);
							}
							line_start_y = this_line_start_y;
							line_end_y = this_line_end_y;
							drawing_line = 2;
							break;
						case 2:
							line_end_y = this_line_end_y;
							break;
						default:
							ASSERT(0);
							break;
						}
					}
				} else {
					if (drawing_line) {
						line.start = v2((f32)x + 0.5f, line_start_y);
						line.end   = v2((f32)x + 0.5f,   line_end_y);
						if (line.start != line.end) {
							physics.static_lines.append(line);
						}
						drawing_line = 0;
					}
				}
			}
		}
	}

	for (u32 i = 0; i < num_entities; ++i) {
		Entity *e = &entities[i];
		Physics_Static_Circle circle = {};
		circle.owner_id = e->id;
		circle.collision_mask = collision_mask_entity;
		circle.collides_with_mask = collides_with_entity;
		circle.radius = constants.physics.entity_radius;
		circle.pos = (v2)e->pos;
		physics.static_circles.append(circle);
	}

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
		case ACTION_FIRE_BOLT: {
			Transaction t = {};
			t.type = TRANSACTION_FIRE_BOLT_CAST;
			t.start_time = 0.0f;
			t.fire_bolt.caster_id = action.fire_bolt.caster_id;
			t.fire_bolt.target_id = action.fire_bolt.target_id;
			transactions.append(t);
			break;
		}
		case ACTION_HEAL: {
			Transaction t = {};
			t.type = TRANSACTION_HEAL_CAST;
			t.heal.caster_id = action.heal.caster_id;
			t.heal.target_id = action.heal.target_id;
			t.heal.amount = action.heal.amount;
			transactions.append(t);
			break;
		}
		case ACTION_LIGHTNING: {
			Transaction t = {};
			t.type = TRANSACTION_LIGHTNING_CAST;
			t.lightning.caster_id = action.lightning.caster_id;
			t.lightning.start = action.lightning.start;
			t.lightning.end = action.lightning.end;
			transactions.append(t);
			break;
		}
		}
	}

	// 2. initialise "occupied" grid

	Map_Cache_Bool occupied;
	occupied.reset();

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
		v2        pos;
	};
	Max_Length_Array<Entity_Damage, MAX_ENTITIES> entity_damage;

#define MAX_PROJECTILES 1024
	enum Projectile_Type
	{
		PROJECTILE_FIREBALL_SHOT,
		PROJECTILE_FIREBALL_OFFSHOOT,
		PROJECTILE_LIGHTNING_BOLT,
	};
	struct Projectile
	{
		Projectile_Type type;
		Entity_ID entity_id;
		// TODO -- count instances associated with projectile in order to delete projectile
		// when no instances left
		u32 count;
		f32 last_anim_built;
	};
	Max_Length_Array<Projectile, MAX_PROJECTILES> projectiles;
	projectiles.reset();

	struct Entities_Hit_By_Projectile
	{
		Entity_ID projectile_id;
		Entity_ID entity_id;
	};
	Max_Length_Array<Entities_Hit_By_Projectile, MAX_PROJECTILES> entities_hit_by_projectile;
	entities_hit_by_projectile.reset();

#define MAX_PHYSICS_EVENTS 1024
	Max_Length_Array<Physics_Event, MAX_PHYSICS_EVENTS> physics_events;
	physics_events.reset();

	physics_start_frame(&physics);
	physics_compute_collisions(&physics, 0.0f, physics_events);

	f32 time = 0.0f;
	u8 physics_processing_left = 1;
	while (transactions || physics_processing_left) {
		u8 recompute_physics_collisions = 0;
		physics_processing_left = 0;
		entity_damage.reset();

		// draw physics state
		if (0) {
			debug_draw_world_reset();
			physics_debug_draw(&physics, time);

			debug_draw_world_set_color(v4(0.0f, 0.0f, 1.0f, 0.75f));
			for (u32 i = 0; i < physics_events.len; ++i) {
				Physics_Event pe = physics_events[i];
				if (pe.time != time) {
					continue;
				}
				switch (pe.type) {
				case PHYSICS_EVENT_BEGIN_PENETRATE_SL_LC:
				case PHYSICS_EVENT_END_PENETRATE_SL_LC: {
					Physics_Static_Line *line = pe.sl_lc.line;
					Physics_Linear_Circle *circle = pe.sl_lc.circle;
					debug_draw_world_line(line->start, line->end);
					v2 p = circle->start;
					v2 v = circle->velocity;
					f32 r = circle->radius;
					f32 t = pe.time - circle->start_time;
					debug_draw_world_circle(p + t * v, r);
					break;
				}
				case PHYSICS_EVENT_BEGIN_PENETRATE_SC_LC:
				case PHYSICS_EVENT_END_PENETRATE_SC_LC: {
					Physics_Static_Circle *circle_1 = pe.sc_lc.static_circle;
					Physics_Linear_Circle *circle_2 = pe.sc_lc.linear_circle;
					debug_draw_world_circle(circle_1->pos, circle_1->radius);
					v2 p = circle_2->start;
					v2 v = circle_2->velocity;
					f32 r = circle_2->radius;
					f32 t = pe.time - circle_2->start_time;
					debug_draw_world_circle(p + t * v, r);
					break;
				}
				case PHYSICS_EVENT_BEGIN_PENETRATE_LC_LC:
				case PHYSICS_EVENT_END_PENETRATE_LC_LC: {
					Physics_Linear_Circle *circle_1 = pe.lc_lc.circle_1;
					Physics_Linear_Circle *circle_2 = pe.lc_lc.circle_2;
					v2 p = circle_1->start;
					v2 v = circle_1->velocity;
					f32 r = circle_1->radius;
					f32 t = pe.time - circle_1->start_time;
					debug_draw_world_circle(p + t * v, r);
					p = circle_2->start;
					v = circle_2->velocity;
					r = circle_2->radius;
					t = pe.time - circle_2->start_time;
					debug_draw_world_circle(p + t * v, r);
					break;
				}
				default:
					continue;
				}
			}

			DPLOG("Time: %f, %x", time, *(u32*)&time);
			debug_pause();
		}

		for (u32 i = 0; i < physics_events.len; ++i) {
			Physics_Event pe = physics_events[i];
			if (pe.time < time) {
				continue;
			}
			if (pe.time > time) {
				physics_processing_left = 1;
				break;
			}
			switch (pe.type) {
			case PHYSICS_EVENT_BEGIN_PENETRATE_SL_LC: {
				if (!pe.sl_lc.circle->owner_id) {
					break;
				}
				u32 projectile_idx = projectiles.len;
				for (u32 i = 0; i < projectiles.len; ++i) {
					if (projectiles[i].entity_id == pe.sl_lc.circle->owner_id) {
						projectile_idx = i;
						break;
					}
				}
				ASSERT(projectile_idx < projectiles.len);
				Projectile *p = &projectiles[projectile_idx];
				switch (p->type) {
				case PROJECTILE_FIREBALL_OFFSHOOT:
					if (pe.sl_lc.line->owner_id == ENTITY_ID_WALLS) {
						recompute_physics_collisions = 1;
						pe.sl_lc.circle->owner_id = 0;
						f32 start_time = pe.sl_lc.circle->start_time;
						f32 duration = time - start_time;
						v2 p = pe.sl_lc.circle->start;
						v2 v = pe.sl_lc.circle->velocity;
						Event e = {};
						e.type = EVENT_FIREBALL_OFFSHOOT_2;
						e.time = start_time;
						e.fireball_offshoot_2.duration = duration;
						e.fireball_offshoot_2.start = p;
						e.fireball_offshoot_2.end = p + duration * v;
						events.append(e);
					}
					break;
				case PROJECTILE_LIGHTNING_BOLT:
					if (pe.sl_lc.line->owner_id == ENTITY_ID_WALLS) {
						recompute_physics_collisions = 1;

						Physics_Static_Line *l = pe.sl_lc.line;
						Physics_Linear_Circle *c = pe.sl_lc.circle;
						if (p->last_anim_built < time) {
							f32 start = max(p->last_anim_built,
							                c->start_time);
							f32 end = time;
							p->last_anim_built = time;
							v2 start_pos = c->start
							    + (start - c->start_time) * c->velocity;
							v2 end_pos = c->start
							    + (end - c->start_time) * c->velocity;

							Event e = {};
							e.type = EVENT_LIGHTNING_BOLT;
							e.time = start;
							e.lightning_bolt.start = start_pos;
							e.lightning_bolt.end = end_pos;
							e.lightning_bolt.duration = end - start;
							events.append(e);
						}
						// reflect start/end of the linear circle through line
						v2 d = l->end - l->start;
						d = d / sqrtf(d.x*d.x + d.y*d.y);
						v2 new_start = c->start + (time - c->start_time) * c->velocity;
						v2 p_0 = c->start - l->start;
						v2 p_1 = c->start + c->duration * c->velocity - l->start;
						p_0 = 2.0f * dot(p_0, d) * d - p_0;
						p_1 = 2.0f * dot(p_1, d) * d - p_1;
						c->velocity = (p_1 - p_0) / c->duration;
						c->start = new_start;
						c->duration -= time - c->start_time;
						c->start_time = time;
					}
					break;
				default:
					ASSERT(0);
					break;
				}
				break;
			}
			case PHYSICS_EVENT_BEGIN_PENETRATE_SC_LC: {
				u32 projectile_idx = projectiles.len;
				if (!pe.sc_lc.linear_circle->owner_id) {
					break;
				}
				for (u32 i = 0; i < projectiles.len; ++i) {
					if (projectiles[i].entity_id == pe.sc_lc.linear_circle->owner_id) {
						projectile_idx = i;
						break;
					}
				}
				ASSERT(projectile_idx < projectiles.len);
				Projectile *p = &projectiles[projectile_idx];
				switch (p->type) {
				case PROJECTILE_FIREBALL_OFFSHOOT: {
					Entity_ID entity_id = pe.sc_lc.static_circle->owner_id;
					Entity *e = game_get_entity_by_id(game, entity_id);
					if (e) {
						for (u32 i = 0; i < entities_hit_by_projectile.len; ++i) {
							Entities_Hit_By_Projectile hit = entities_hit_by_projectile[i];
							if (hit.entity_id == e->id
							 && hit.projectile_id == p->entity_id) {
								goto break_do_damage;
							}
						}

						Entities_Hit_By_Projectile hit = {};
						hit.entity_id = e->id;
						hit.projectile_id = p->entity_id;
						entities_hit_by_projectile.append(hit);

						Entity_Damage ed = {};
						ed.entity_id = entity_id;
						ed.pos = pe.sc_lc.static_circle->pos;
						ed.damage = 1;
						entity_damage.append(ed);
					}
				break_do_damage: ;
					break;
				}
				case PROJECTILE_LIGHTNING_BOLT: {
					Entity_ID entity_id = pe.sc_lc.static_circle->owner_id;
					Entity *e = game_get_entity_by_id(game, entity_id);
					if (e) {
						for (u32 i = 0; i < entities_hit_by_projectile.len; ++i) {
							Entities_Hit_By_Projectile hit = entities_hit_by_projectile[i];
							if (hit.entity_id == e->id
							 && hit.projectile_id == p->entity_id) {
								goto break_do_damage_2;
							}
						}

						Entities_Hit_By_Projectile hit = {};
						hit.entity_id = e->id;
						hit.projectile_id = p->entity_id;
						entities_hit_by_projectile.append(hit);

						Entity_Damage ed = {};
						ed.entity_id = entity_id;
						ed.pos = pe.sc_lc.static_circle->pos;
						ed.damage = 1;
						entity_damage.append(ed);
					}
				break_do_damage_2: ;
					break;
				}
				default:
					ASSERT(0);
					break;
				}
				break;
			}
			case PHYSICS_EVENT_END_PENETRATE_SC_LC: {
				u32 projectile_idx = projectiles.len;
				if (!pe.sc_lc.linear_circle->owner_id) {
					break;
				}
				for (u32 i = 0; i < projectiles.len; ++i) {
					if (projectiles[i].entity_id == pe.sc_lc.linear_circle->owner_id) {
						projectile_idx = i;
						break;
					}
				}
				ASSERT(projectile_idx < projectiles.len);
				Projectile *p = &projectiles[projectile_idx];
				switch (p->type) {
				case PROJECTILE_LIGHTNING_BOLT:
					Entity_ID entity_id = pe.sc_lc.static_circle->owner_id;
					Entity *e = game_get_entity_by_id(game, entity_id);
					if (e) {
						for (u32 i = 0; i < entities_hit_by_projectile.len; ++i) {
							Entities_Hit_By_Projectile hit = entities_hit_by_projectile[i];
							if (hit.entity_id == e->id
							 && hit.projectile_id == p->entity_id) {
							 	entities_hit_by_projectile.remove(i);
								break;
							}
						}
					}
					break;
				}
				break;
			}
			case PHYSICS_EVENT_BEGIN_PENETRATE_LC_LC: {
				u32 projectile_idx_1 = projectiles.len;
				u32 projectile_idx_2 = projectiles.len;
				for (u32 i = 0; i < projectiles.len; ++i) {
					u32 projectile_entity_id = projectiles[i].entity_id;
					if (projectile_entity_id == pe.lc_lc.circle_1->owner_id) {
						projectile_idx_1 = i;
						if (projectile_idx_2 < projectiles.len) {
							break;
						}
					}
					if (projectile_entity_id == pe.lc_lc.circle_2->owner_id) {
						projectile_idx_2 = i;
						if (projectile_idx_1 < projectiles.len) {
							break;
						}
					}
					// TODO -- collision between "entity" and projectile
				}
				break;
			}
			case PHYSICS_EVENT_LC_END: {
				if (!pe.lc.circle->owner_id) {
					break;
				}
				u32 projectile_idx = projectiles.len;
				for (u32 i = 0; i < projectiles.len; ++i) {
					if (projectiles[i].entity_id == pe.lc.circle->owner_id) {
						projectile_idx = i;
						break;
					}
				}
				if(projectile_idx < projectiles.len) {
					switch (projectiles[projectile_idx].type) {
					case PROJECTILE_FIREBALL_OFFSHOOT: {
						recompute_physics_collisions = 1;
						pe.lc.circle->owner_id = 0;
						f32 start_time = pe.lc.circle->start_time;
						f32 duration = time - start_time;
						v2 p = pe.lc.circle->start;
						v2 v = pe.lc.circle->velocity;
						Event e = {};
						e.type = EVENT_FIREBALL_OFFSHOOT_2;
						e.time = start_time;
						e.fireball_offshoot_2.duration = duration;
						e.fireball_offshoot_2.start = p;
						e.fireball_offshoot_2.end = p + duration * v;
						events.append(e);
						break;
					}
					case PROJECTILE_LIGHTNING_BOLT: {
						recompute_physics_collisions = 1;

						Physics_Linear_Circle *c = pe.lc.circle;
						Projectile *p = &projectiles[projectile_idx];

						f32 start = max(p->last_anim_built, c->start_time);
						f32 end = time;
						p->last_anim_built = time;
						v2 start_pos = c->start
						    + (start - c->start_time) * c->velocity;
						v2 end_pos = c->start
						    + (end - c->start_time) * c->velocity;

						Event e = {};
						e.type = EVENT_LIGHTNING_BOLT;
						e.time = start;
						e.lightning_bolt.start = start_pos;
						e.lightning_bolt.end = end_pos;
						e.lightning_bolt.duration = end - start;
						events.append(e);
						break;
					}
					default:
						ASSERT(0);
					}
				}
				break;
			}
			}
		}

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
				debug_draw_world_set_color(v4(1.0f, 1.0f, 0.0f, 1.0f));
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
				debug_draw_world_set_color(v4(0.0f, 1.0f, 0.0f, 1.0f));
				debug_draw_world_arrow((v2)m.move.start, (v2)m.move.end);
				debug_pause();
#endif

				// post-exit
				m.type = MESSAGE_MOVE_POST_EXIT;
				game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);

				physics_remove_objects_for_entity(&physics, t->move.entity_id);
				Physics_Linear_Circle circle = {};
				circle.collision_mask = collision_mask_entity;
				circle.collides_with_mask = collides_with_entity;
				circle.owner_id = t->move.entity_id;
				circle.start = (v2)t->move.start;
				circle.velocity = ((v2)t->move.end - (v2)t->move.start) / constants.anims.move.duration;
				circle.start_time = time;
				circle.duration = constants.anims.move.duration;
				circle.radius = constants.physics.entity_radius;
				physics.linear_circles.append(circle);
				recompute_physics_collisions = 1;

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
				debug_draw_world_set_color(v4(1.0f, 1.0f, 0.0f, 1.0f));
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
					debug_draw_world_set_color(v4(0.0f, 1.0f, 0.0f, 1.0f));
					debug_draw_world_arrow((v2)start, (v2)end);
					debug_pause();
#endif
				} else {
#ifdef DEBUG_TRANSACTION_PROCESSING
					debug_draw_world_reset();
					debug_draw_world_set_color(v4(1.0f, 0.0f, 0.0f, 1.0f));
					debug_draw_world_circle((v2)start, 0.5f);
					debug_pause();
#endif
					recompute_physics_collisions = 1;
					physics_remove_objects_for_entity(&physics, t->move.entity_id);
					Physics_Linear_Circle circle = {};
					circle.collision_mask = collision_mask_entity;
					circle.collides_with_mask = collides_with_entity;
					circle.owner_id = t->move.entity_id;
					circle.start = (v2)t->move.end;
					circle.velocity = ((v2)t->move.start - (v2)t->move.end) / constants.anims.move.duration;
					circle.start_time = time - constants.anims.move.duration / 2.0f;
					circle.duration = constants.anims.move.duration;
					circle.radius = constants.physics.entity_radius;
					physics.linear_circles.append(circle);

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

				recompute_physics_collisions = 1;
				physics_remove_objects_for_entity(&physics, t->move.entity_id);
				Physics_Static_Circle circle = {};
				circle.collision_mask = collision_mask_entity;
				circle.collides_with_mask = collides_with_entity;
				circle.owner_id = t->move.entity_id;
				circle.pos = (v2)t->move.end;
				circle.radius = constants.physics.entity_radius;
				physics.static_circles.append(circle);

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

				t->type = TRANSACTION_FIREBALL_HIT;
				t->start_time = time + constants.anims.fireball.shot_duration;
				break;
			}
			case TRANSACTION_FIREBALL_HIT: {
				Event event = {};
				event.type = EVENT_FIREBALL_HIT;
				event.time = time;
				event.fireball_hit.pos = t->fireball_shot.end;
				events.append(event);

				t->type = TRANSACTION_REMOVE;

				Projectile projectile = {};
				projectile.type = PROJECTILE_FIREBALL_OFFSHOOT;
				projectile.entity_id = game_new_entity_id(game);
				projectiles.append(projectile);

				recompute_physics_collisions = 1;
				Physics_Linear_Circle circle = {};
				circle.owner_id = projectile.entity_id;
				circle.collision_mask = collision_mask_projectile;
				circle.collides_with_mask = collides_with_projectile;
				circle.start = (v2)t->fireball_shot.end;
				circle.radius = constants.physics.fireball_radius;
				circle.duration = constants.physics.fireball_duration;
				circle.start_time = time + TRANSACTION_EPSILON;
				for (u32 i = 0; i < 5; ++i) {
					f32 angle = PI_F32 * ((f32)i / 5.0f) / 2.0f;
					f32 ca = cosf(angle);
					f32 sa = sinf(angle);

					circle.velocity = 5.0f * v2( ca,  sa);
					physics.linear_circles.append(circle);

					circle.velocity = 5.0f * v2(-sa,  ca);
					physics.linear_circles.append(circle);

					circle.velocity = 5.0f * v2(-ca, -sa);
					physics.linear_circles.append(circle);

					circle.velocity = 5.0f * v2( sa, -ca);
					physics.linear_circles.append(circle);
				}

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

				Entity *e = game_get_entity_by_id(game, t->blink.caster_id);

				// TODO -- should check if pos is free to enter?
				Pos start = e->pos;
				Pos end = t->blink.target;

				Event event = {};
				event.type = EVENT_BLINK;
				event.time = time;
				event.blink.caster_id = t->blink.caster_id;
				event.blink.start = start;
				event.blink.target = end;
				events.append(event);

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

				Entity_Damage damage = {};
				damage.entity_id = e_id;
				damage.damage = 3;
				damage.pos = (v2)e->pos;
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
						v2_i16 end = start + v2_i16(dx, dy);
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
			case TRANSACTION_FIRE_BOLT_CAST: {
				Entity *caster = game_get_entity_by_id(game, t->fire_bolt.caster_id);
				Entity *target = game_get_entity_by_id(game, t->fire_bolt.target_id);
				if (!caster || !target) {
					t->type = TRANSACTION_REMOVE;
					break;
				}
				t->type = TRANSACTION_FIRE_BOLT_SHOT;
				t->start_time += constants.anims.fire_bolt.cast_time;
				Pos caster_pos = caster->pos;
				Pos target_pos = target->pos;
				t->fire_bolt.start = caster_pos;
				t->fire_bolt.end = target_pos;
				break;
			}
			case TRANSACTION_FIRE_BOLT_SHOT: {
				t->type = TRANSACTION_FIRE_BOLT_HIT;
				v2 p = (v2)t->fire_bolt.start - (v2)t->fire_bolt.end;
				f32 d = sqrtf(p.x*p.x + p.y*p.y);
				f32 shot_duration = d / constants.anims.fire_bolt.speed;
				t->start_time += shot_duration;

				Event e = {};
				e.type = EVENT_FIRE_BOLT_SHOT;
				e.time = time;
				e.fire_bolt_shot.start = (v2)t->fire_bolt.start;
				e.fire_bolt_shot.end   = (v2)t->fire_bolt.end;
				e.fire_bolt_shot.duration = shot_duration;
				events.append(e);

				break;
			}
			case TRANSACTION_FIRE_BOLT_HIT: {
				t->type = TRANSACTION_REMOVE;
				Entity *target = game_get_entity_by_id(game, t->fire_bolt.target_id);
				if (!target) {
					break;
				}
				Pos target_pos = target->pos;
				if (target_pos != t->fire_bolt.end) {
					break;
				}

				Entity_Damage damage = {};
				damage.entity_id = target->id;
				damage.damage = 3;
				damage.pos = (v2)target_pos;
				entity_damage.append(damage);

				break;
			}
			case TRANSACTION_HEAL_CAST: {
				t->type = TRANSACTION_HEAL;
				t->start_time += constants.anims.heal.cast_time;
				Entity *caster = game_get_entity_by_id(game, t->heal.caster_id);
				if (!caster) {
					t->type = TRANSACTION_REMOVE;
					break;
				}
				t->heal.start = caster->pos;
				break;
			}
			case TRANSACTION_HEAL: {
				t->type = TRANSACTION_REMOVE;
				Entity *target = game_get_entity_by_id(game, t->heal.target_id);
				if (!target) {
					break;
				}
				target->hit_points += t->heal.amount;
				target->hit_points = min(target->hit_points, target->max_hit_points);

				Event e = {};
				e.type = EVENT_HEAL;
				e.time = time - constants.anims.heal.cast_time;
				e.heal.caster_id = t->heal.caster_id;
				e.heal.target_id = t->heal.target_id;
				e.heal.amount = t->heal.amount;
				ASSERT(t->heal.amount);
				e.heal.start = (v2)t->heal.start;
				e.heal.end = (v2)target->pos;
				events.append(e);
				break;
			}
			case TRANSACTION_LIGHTNING_CAST: {
				t->type = TRANSACTION_LIGHTNING_SHOT;
				t->start_time += constants.anims.lightning.cast_time;
				Entity *caster = game_get_entity_by_id(game, t->lightning.caster_id);
				if (!caster) {
					t->type = TRANSACTION_REMOVE;
					break;
				}
				t->lightning.start = caster->pos;
				break;
			}
			case TRANSACTION_LIGHTNING_SHOT: {
				t->type = TRANSACTION_REMOVE;

				Projectile projectile = {};
				projectile.type = PROJECTILE_LIGHTNING_BOLT;
				projectile.entity_id = game_new_entity_id(game);
				projectiles.append(projectile);

				Entities_Hit_By_Projectile hit = {};
				hit.projectile_id = projectile.entity_id;
				hit.entity_id = t->lightning.caster_id;
				entities_hit_by_projectile.append(hit);

				recompute_physics_collisions = 1;
				Physics_Linear_Circle circle = {};
				circle.owner_id = projectile.entity_id;
				circle.collision_mask = collision_mask_projectile;
				circle.collides_with_mask = collides_with_projectile;
				circle.start = (v2)t->lightning.start;
				circle.radius = constants.physics.lightning_radius;
				circle.duration = constants.physics.lightning_duration;
				circle.start_time = time + TRANSACTION_EPSILON;

				v2 dir = (v2)t->lightning.end - (v2)t->lightning.start;
				dir = dir / sqrtf(dir.x*dir.x + dir.y*dir.y);
				circle.velocity = constants.physics.lightning_distance * dir;

				physics.linear_circles.append(circle);

				Event e = {};
				e.type = EVENT_LIGHTNING_BOLT_START;
				e.time = time;
				e.lightning_bolt_start.pos = (v2)t->lightning.start;
				events.append(e);
				break;
			}
			}
		}


		// merge damage events
		for (u32 i = 0; i < entity_damage.len; ++i) {
			Entity_Damage *ed = &entity_damage[i];
			for (u32 j = i + 1; j < entity_damage.len; ) {
				Entity_Damage *this_ed = &entity_damage[j];
				if (this_ed->entity_id == ed->entity_id) {
					ed->damage += this_ed->damage;
					entity_damage.remove(j);
					continue;
				}
				++j;
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

			if (entity_died) {
				Message m = {};
				m.type = MESSAGE_PRE_DEATH;
				m.death.entity_id = entity_id;
				game_dispatch_message(game, m, time, &transactions, event_buffer, NULL);

				Event death_event = {};
				death_event.type = EVENT_DEATH;
				death_event.time = time;
				death_event.death.entity_id = entity_id;
				events.append(death_event);
				// *e = game->entities[--game->num_entities];
				game_remove_entity(game, entity_id);
			}
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

		if (recompute_physics_collisions) {
			recompute_physics_collisions = 0;
			physics_start_frame(&physics);
			physics_compute_collisions(&physics, time, physics_events);
		}

		// TODO - need some better representation of "infinity"
		f32 next_time = 1000000.0f;
		for (u32 i = 0; i < physics_events.len; ++i) {
			Physics_Event pe = physics_events[i];
			if (pe.time <= time) {
				continue;
			}
			next_time = pe.time;
			physics_processing_left = 1;
			break;
		}

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

	Entity_ID player_id;
	{
		Controller *c = &game->controllers[0];
		ASSERT(c->type == CONTROLLER_PLAYER);
		player_id = c->player.entity_id;
	}

	// XXX - tmp - recalculate FOV
	{
		game->field_of_vision.reset();
		calculate_field_of_vision(game, player_id, &game->field_of_vision);
		Event e = {};
		e.type = EVENT_FIELD_OF_VISION_CHANGED;
		e.time = 0.0f; // XXX
		e.field_of_vision.duration = constants.anims.move.duration;
		e.field_of_vision.fov = &game->field_of_vision;
		event_buffer->append(e);
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
					Pos end = (Pos)((v2_i16)start + v2_i16(dx, dy));
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
					v2_i16 end = start + v2_i16(dx, dy);
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
		case CONTROLLER_LICH: {
			if (!lich_get_skeleton_to_heal(game, c)) {
				Entity *e = game_get_entity_by_id(game, c->lich.lich_id);
				u16 move_mask = e->movement_type;
				Pos start = e->pos;
				for (i8 dy = -1; dy <= 1; ++dy) {
					for (i8 dx = -1; dx <= 1; ++dx) {
						if (!(dx || dy)) {
							continue;
						}
						Pos end = (Pos)((v2_i16)start + v2_i16(dx, dy));
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
			}
			auto& skeleton_ids = c->lich.skeleton_ids;
			u32 num_skeleton_ids = skeleton_ids.len;
			for (u32 i = 0; i < num_skeleton_ids; ++i) {
				Entity *e = game_get_entity_by_id(game, skeleton_ids[i]);
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
						v2_i16 end = start + v2_i16(dx, dy);
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
			}
			break;
		}
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
	debug_draw_world_set_color(v4(1.0f, 0.0f, 0.0f, 1.0f));
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
		debug_draw_world_set_color(v4(pm.weight, 0.0f, 0.0f, 1.0f));
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
			debug_draw_world_set_color(v4(pm.weight, 0.0f, 0.0f, 1.0f));
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
			debug_draw_world_set_color(v4(1.0f, 1.0f, 0.0f, 1.0f));
			debug_draw_world_arrow((v2)a.fireball.start, (v2)a.fireball.end);
			debug_pause();
			*/

			actions.append(a);
			break;
		}
		case CONTROLLER_LICH: {
			Entity_ID skeleton_to_heal = lich_get_skeleton_to_heal(game, c);
			if (skeleton_to_heal) {
				c->lich.heal_cooldown = 5;

				Action a = {};
				a.type = ACTION_HEAL;
				a.heal.caster_id = c->lich.lich_id;
				a.heal.target_id = skeleton_to_heal;
				a.heal.amount = 5;
				actions.append(a);
			} else if (c->lich.heal_cooldown) {
				--c->lich.heal_cooldown;
			}
			break;
		}
		}
	}

#ifdef DEBUG_SHOW_ACTIONS
	debug_draw_world_reset();
	debug_draw_world_set_color(v4(0.0f, 1.0f, 1.0f, 1.0f));
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

	Entity_ID e_id = NUM_STATIC_ENTITY_IDS;

	Pos cur_pos = { 1, 1 };
	Controller_ID c_id = 1;

	Controller *player_controller = game->controllers.items;
	++game->controllers.len;
	player_controller->type = CONTROLLER_PLAYER;
	player_controller->id = c_id++;
	auto& tiles = game->tiles;
	auto& handlers = game->handlers;
	memset(&tiles, 0, sizeof(tiles));

	Controller lich_controller_tmp = {};
	lich_controller_tmp.type = CONTROLLER_LICH;
	Controller *lich_controller = &lich_controller_tmp;

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
		case 'L': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 10;
			e.max_hit_points = 10;
			e.id = e_id++;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_CREATURE_NECROMANCER;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.movement_type = BLOCK_WALK;
			game->entities.append(e);

			lich_controller->id = c_id++;
			lich_controller->lich.lich_id = e.id;
			game->controllers.append(*lich_controller);
			lich_controller = &game->controllers[game->controllers.len - 1];

			Message_Handler mh = {};
			mh.type = MESSAGE_HANDLER_LICH_DEATH;
			mh.handle_mask = MESSAGE_PRE_DEATH;
			mh.owner_id = e.id;
			mh.lich_death.controller_id = lich_controller->id;
			handlers.append(mh);

			break;
		}
		case 'S': {
			tiles[cur_pos].type = TILE_FLOOR;
			tiles[cur_pos].appearance = APPEARANCE_FLOOR_ROCK;

			Entity e = {};
			e.hit_points = 10;
			e.max_hit_points = 10;
			e.id = e_id++;
			e.pos = cur_pos;
			e.appearance = APPEARANCE_CREATURE_SKELETON;
			e.block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
			e.movement_type = BLOCK_WALK;
			game->entities.append(e);

			lich_controller->lich.skeleton_ids.append(e.id);
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
			e.id = ENTITY_ID_PLAYER;
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

	calculate_field_of_vision(game, player_controller->player.entity_id, &game->field_of_vision);
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

/*
struct Camera
{
	v2  world_center;
	v2  offset;
	f32 zoom;
};

struct Draw
{
	Camera camera;
	Field_Of_Vision_Render fov_render;
	Sprite_Sheet_Renderer  renderer;
	Sprite_Sheet_Instances tiles;
	Sprite_Sheet_Instances creatures;
	Sprite_Sheet_Instances water_edges;
	Sprite_Sheet_Instances effects_24;
	Sprite_Sheet_Instances effects_32;
	Sprite_Sheet_Font_Instances boxy_bold;
	Card_Render card_render;
	Debug_Line card_debug_line;
};
*/

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
	ANIM_PROJECTILE_EFFECT_24,
	ANIM_PROJECTILE_EFFECT_32,
	ANIM_TEXT,
	ANIM_CAMERA_SHAKE,
	ANIM_SOUND,
	ANIM_DEATH,
	ANIM_EXCHANGE,
	ANIM_BLINK,
	ANIM_BLINK_PARTICLES,
	ANIM_SLIME_SPLIT,
	ANIM_POLYMORPH,
	ANIM_POLYMORPH_PARTICLES,
	ANIM_HEAL_PARTICLES,
	ANIM_FIELD_OF_VISION_CHANGED,
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
			v2 sprite_coords;
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
			v2 pos;
		} blink_particles;
		struct {
			f32 time;
			f32 duration;
			Entity_ID original_id;
			Entity_ID new_id;
			v2 start;
			v2 end;
		} slime_split;
		struct {
			f32 start_time;
			Entity_ID entity_id;
			v2        pos;
			v2        new_sprite_coords;
		} polymorph;
		struct {
			f32 start_time;
			v2  pos;
		} polymorph_particles;
		struct {
			f32 start_time;
			f32 duration;
			v2 start;
			v2 end;
		} heal_particles;
		struct {
			f32 start_time;
			f32 duration;
			u32 buffer_id;
			Map_Cache_Bool *fov;
		} field_of_vision;
	};
	v2 sprite_coords;
	v2 world_coords;
	Entity_ID entity_id;
	f32 depth_offset;
};

u8 anim_is_active(Anim* anim)
{
	switch (anim->type) {
	case ANIM_TILE_STATIC:             return 0;
	case ANIM_WATER_EDGE:              return 0;
	case ANIM_CREATURE_IDLE:           return 0;
	case ANIM_MOVE:                    return 1;
	case ANIM_MOVE_BLOCKED:            return 1;
	case ANIM_DROP_TILE:               return 1;
	case ANIM_PROJECTILE_EFFECT_24:    return 1;
	case ANIM_PROJECTILE_EFFECT_32:    return 1;
	case ANIM_TEXT:                    return 1;
	case ANIM_CAMERA_SHAKE:            return 1;
	case ANIM_SOUND:                   return 1;
	case ANIM_DEATH:                   return 1;
	case ANIM_EXCHANGE:                return 1;
	case ANIM_BLINK:                   return 1;
	case ANIM_BLINK_PARTICLES:         return 1;
	case ANIM_SLIME_SPLIT:             return 1;
	case ANIM_POLYMORPH:               return 1;
	case ANIM_POLYMORPH_PARTICLES:     return 1;
	case ANIM_HEAL_PARTICLES:          return 1;
	case ANIM_FIELD_OF_VISION_CHANGED: return 1;
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

f32 angle_to_sprite_x_coord(f32 angle)
{
	angle /= PI_F32;
	f32 x;
	if      (angle < -0.875f) { x = 2.0f; }
	else if (angle < -0.625f) { x = 5.0f; }
	else if (angle < -0.375f) { x = 3.0f; }
	else if (angle < -0.125f) { x = 4.0f; }
	else if (angle <  0.125f) { x = 0.0f; }
	else if (angle <  0.375f) { x = 7.0f; }
	else if (angle <  0.625f) { x = 1.0f; }
	else if (angle <  0.875f) { x = 6.0f; }
	else                      { x = 2.0f; }
	return x;
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

			a = {};
			a.type = ANIM_SOUND;
			a.sound.start_time = event->time;
			a.sound.sound_id = SOUND_FIRE_SPELL_02;
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
		case EVENT_FIREBALL_OFFSHOOT_2: {
			Anim a = {};
			a.type = ANIM_PROJECTILE_EFFECT_24;
			a.projectile.start_time = event->time;
			a.projectile.duration = event->fireball_offshoot_2.duration;
			a.projectile.start = event->fireball_offshoot_2.start;
			a.projectile.end = event->fireball_offshoot_2.end;
			v2 dir = a.projectile.end - a.projectile.start;
			f32 angle = atan2f(-dir.y, dir.x);
			a.projectile.sprite_coords = { angle_to_sprite_x_coord(angle), 11.0f };
			anims.append(a);
			break;
		}
		case EVENT_LIGHTNING_BOLT: {
			Anim a = {};
			a.type = ANIM_PROJECTILE_EFFECT_24;
			a.projectile.start_time = event->time;
			a.projectile.duration = event->lightning_bolt.duration;
			a.projectile.start = event->lightning_bolt.start;
			a.projectile.end = event->lightning_bolt.end;
			a.projectile.sprite_coords = { 0.0f, 9.0f };
			anims.append(a);
			break;
		}
		case EVENT_LIGHTNING_BOLT_START: {
			Anim a = {};
			a.type = ANIM_SOUND;
			a.sound.start_time = event->time;
			a.sound.sound_id = SOUND_LIGHTNING_SPELL_03;
			anims.append(a);
			break;
		}
		case EVENT_STUCK: {
			Anim text_anim = {};
			text_anim.type = ANIM_TEXT;
			text_anim.text.start_time = event->time;
			text_anim.text.duration = constants.anims.text_duration;
			text_anim.text.color = v4(1.0f, 1.0f, 1.0f, 1.0f);
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
			text_anim.text.color = v4(1.0f, 0.0f, 0.0f, 1.0f);

			char *buffer = fmt("-%u", event->damaged.amount);
			u32 i = 0;
			for (char *p = buffer; *p; ++p, ++i) {
				text_anim.text.caption[i] = (u8)*p;
			}
			ASSERT(i < ARRAY_SIZE(text_anim.text.caption));
			text_anim.text.caption[i] = 0;
			text_anim.world_coords = event->damaged.pos;
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

			f32 particle_start = event->time - constants.anims.blink.particle_start;
			anim = {};
			anim.type = ANIM_BLINK_PARTICLES;
			anim.blink_particles.time = particle_start;
			anim.blink_particles.pos = (v2)event->blink.start;
			world_anim->anims.append(anim);

			anim = {};
			anim.type = ANIM_BLINK_PARTICLES;
			anim.blink_particles.time = event->time;
			anim.blink_particles.pos = (v2)event->blink.target;
			world_anim->anims.append(anim);

			anim = {};
			anim.type = ANIM_SOUND;
			anim.sound.start_time = particle_start;
			anim.sound.sound_id = SOUND_CARD_GAME_ABILITIES_POOF_02;
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
		case EVENT_FIRE_BOLT_SHOT: {
			Anim a = {};
			a.type = ANIM_PROJECTILE_EFFECT_32;
			a.projectile.start_time = event->time;
			a.projectile.duration = event->fire_bolt_shot.duration;
			v2 start = event->fire_bolt_shot.start;
			v2 end = event->fire_bolt_shot.end;
			a.projectile.start = start;
			a.projectile.end = end;
			v2 dir = end - start;
			f32 angle = atan2f(-dir.y, dir.x);
			a.projectile.sprite_coords = { angle_to_sprite_x_coord(angle), 5.0f };
			anims.append(a);

			a = {};
			a.type = ANIM_SOUND;
			a.sound.start_time = event->time;
			a.sound.sound_id = SOUND_FIRE_SPELL_04;
			anims.append(a);
			break;
		}
		case EVENT_POLYMORPH: {
			Anim a = {};
			a.type = ANIM_POLYMORPH;
			a.polymorph.start_time = event->time;
			a.polymorph.entity_id = event->polymorph.entity_id;
			a.polymorph.new_sprite_coords = appearance_get_creature_sprite_coords(
				event->polymorph.new_appearance);
			anims.append(a);

			a = {};
			a.type = ANIM_POLYMORPH_PARTICLES;
			a.polymorph_particles.start_time = event->time;
			a.polymorph_particles.pos = (v2)event->polymorph.pos;
			anims.append(a);

			a = {};
			a.type = ANIM_SOUND;
			a.sound.start_time = event->time;
			a.sound.sound_id = SOUND_SHADOW_SPELL_01;
			anims.append(a);
		}
		case EVENT_HEAL: {
			Anim a = {};
			a.type = ANIM_HEAL_PARTICLES;
			a.heal_particles.start_time = event->time;
			a.heal_particles.duration = constants.anims.heal.cast_time;
			// a.heal_particles.amount = event->heal.amount;
			a.heal_particles.start = (v2)event->heal.start;
			a.heal_particles.end = (v2)event->heal.end;
			anims.append(a);

			a = {};
			a.type = ANIM_TEXT;
			a.text.start_time = event->time + constants.anims.heal.cast_time;
			a.text.duration = 1.0f;
			a.text.color = v4(0.0f, 1.0f, 1.0f, 1.0f);
			snprintf((char*)a.text.caption,
			         ARRAY_SIZE(a.text.caption),
			         "%d",
			         event->heal.amount);
			a.world_coords = (v2)event->heal.end;
			anims.append(a);

			a = {};
			a.type = ANIM_SOUND;
			a.sound.start_time = event->time;
			a.sound.sound_id = SOUND_HEALING_01;
			anims.append(a);
			break;
		}
		case EVENT_FIELD_OF_VISION_CHANGED: {
			Anim a = {};
			a.type = ANIM_FIELD_OF_VISION_CHANGED;
			a.field_of_vision.start_time = event->time;
			a.field_of_vision.duration = event->field_of_vision.duration;
			a.field_of_vision.fov = event->field_of_vision.fov;
			anims.append(a);
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
			Pos p = Pos(x, y);
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
				u8 tl = tiles[(Pos)((v2_i16)p + v2_i16(-1, -1))].appearance == app;
				u8 t  = tiles[(Pos)((v2_i16)p + v2_i16( 0, -1))].appearance == app;
				u8 tr = tiles[(Pos)((v2_i16)p + v2_i16( 1, -1))].appearance == app;
				u8 l  = tiles[(Pos)((v2_i16)p + v2_i16(-1,  0))].appearance == app;
				u8 r  = tiles[(Pos)((v2_i16)p + v2_i16( 1,  0))].appearance == app;
				u8 bl = tiles[(Pos)((v2_i16)p + v2_i16(-1,  1))].appearance == app;
				u8 b  = tiles[(Pos)((v2_i16)p + v2_i16( 0,  1))].appearance == app;
				u8 br = tiles[(Pos)((v2_i16)p + v2_i16( 1,  1))].appearance == app;

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

				if (tiles[(Pos)((v2_i16)p + v2_i16( 0,  1))].type != TILE_WALL) {
					anim.sprite_coords = { 30.0f, 36.0f };
					anim.world_coords = (v2)p + v2(0.0f, 1.0f);
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
				if (tiles[(Pos)((v2_i16)p + v2_i16( 0, -1))].type == t) { mask |= 0x01; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 1, -1))].type == t) { mask |= 0x02; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 1,  0))].type == t) { mask |= 0x04; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 1,  1))].type == t) { mask |= 0x08; }
				if (tiles[(Pos)((v2_i16)p + v2_i16( 0,  1))].type == t) { mask |= 0x10; }
				if (tiles[(Pos)((v2_i16)p + v2_i16(-1,  1))].type == t) { mask |= 0x20; }
				if (tiles[(Pos)((v2_i16)p + v2_i16(-1,  0))].type == t) { mask |= 0x40; }
				if (tiles[(Pos)((v2_i16)p + v2_i16(-1, -1))].type == t) { mask |= 0x80; }

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

	// add field of vision
	{
		Anim a = {};
		a.type = ANIM_FIELD_OF_VISION_CHANGED;
		a.field_of_vision.start_time = 0.0f;
		a.field_of_vision.duration = constants.anims.move.duration;
		a.field_of_vision.buffer_id = 0;
		a.field_of_vision.fov = &game->field_of_vision;
		world_anim->anims.append(a);
	}
}

void world_anim_draw(World_Anim_State* world_anim, Draw* draw, Sound_Player* sound_player, f32 time)
{
	// reset draw state
	sprite_sheet_instances_reset(&draw->tiles);
	sprite_sheet_instances_reset(&draw->creatures);
	sprite_sheet_instances_reset(&draw->water_edges);
	sprite_sheet_instances_reset(&draw->effects_24);
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
		case ANIM_PROJECTILE_EFFECT_24:
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
				continue;
			}
			break;
		case ANIM_BLINK_PARTICLES:
			if (anim->blink_particles.time <= dyn_time) {
				Particle_Instance instance = {};
				Particles *particles = &draw->renderer.particles;
				instance.start_time = time;
				instance.end_time = instance.start_time + constants.anims.blink.particle_duration;
				instance.start_pos = anim->blink_particles.pos;
				instance.start_color = { 0.0f, 1.0f, 1.0f, 1.0f };
				instance.end_color = { 1.0f, 1.0f, 1.0f, 1.0f };

				u32 num_particles = constants.anims.blink.num_particles;
				for (u32 i = 0; i < num_particles; ++i) {
					f32 speed = rand_f32() + 1.0f;
					f32 angle = 2.0f * PI_F32 * rand_f32();
					v2 v = { cosf(angle), sinf(angle) };
					instance.start_velocity = speed * v;
					particles_add(particles, instance);
				}

				anims.remove(i);
				continue;
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
		case ANIM_POLYMORPH:
			if (anim->polymorph.start_time <= dyn_time) {
				Entity_ID e_id = anim->polymorph.entity_id;
				for (u32 i = 0; i < anims.len; ++i) {
					Anim *a = &anims[i];
					if (a->entity_id == e_id) {
						a->sprite_coords = anim->polymorph.new_sprite_coords;
					}
				}
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_POLYMORPH_PARTICLES:
			if (anim->polymorph_particles.start_time <= dyn_time) {
				u32 num_particles = constants.anims.polymorph.num_particles;

				Particles *particles = &draw->renderer.particles;
				Particle_Instance instance = {};
				instance.start_pos = anim->polymorph_particles.pos;
				instance.start_time = time;
				instance.end_time = time + constants.anims.polymorph.duration;
				f32 sin_inner_coeff = constants.anims.polymorph.rotation_speed;
				sin_inner_coeff /= (PI_F32 / 2.0f);
				instance.sin_inner_coeff = v2(sin_inner_coeff, sin_inner_coeff);

				for (u32 i = 0; i < num_particles; ++i) {
					instance.start_color = {
						uniform_f32(0.8f, 1.0f),
						uniform_f32(0.8f, 1.0f),
						uniform_f32(0.8f, 1.0f),
						1.0f
					};
					instance.end_color = {
						uniform_f32(0.8f, 1.0f),
						uniform_f32(0.8f, 1.0f),
						uniform_f32(0.8f, 1.0f),
						1.0f
					};
					f32 angle = 2.0f * PI_F32 * rand_f32();
					instance.sin_phase_offset = v2(PI_F32 / 2.0f, 0.0f) + angle;
					f32 radius = uniform_f32(constants.anims.polymorph.min_radius,
					                         constants.anims.polymorph.max_radius);
					instance.sin_outer_coeff = v2(radius, radius);
					particles_add(particles, instance);
				}
				anims.remove(i);
				continue;
			}
			break;
		case ANIM_HEAL_PARTICLES:
			if (anim->heal_particles.start_time <= dyn_time) {
				u32 num_particles = constants.anims.heal.num_particles;

				Particles *particles = &draw->renderer.particles;
				Particle_Instance instance = {};
				v2 start = anim->heal_particles.start;
				v2 velocity = anim->heal_particles.end - start;
				f32 dist = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y);
				velocity = velocity / constants.anims.heal.cast_time;
				instance.start_color = { 1.0f, 1.0f, 1.0f, 1.0f };
				instance.end_color = { 1.0f, 0.0f, 0.0f, 1.0f };
				f32 start_time = time;
				f32 end_time = time + constants.anims.heal.cast_time;

				for (u32 i = 0; i < num_particles; ++i) {
					f32 radius = uniform_f32(0.1f, 0.5f);
					f32 angle = rand_f32() * PI_F32 / 2.0f;
					v2 offset = radius * v2(cosf(angle), sinf(angle));
					f32 time_offset = rand_f32() * 0.1f;
					instance.start_velocity = velocity;
					instance.start_pos = start + offset;
					instance.start_time= start_time + time_offset;
					instance.end_time = end_time + time_offset;
					particles_add(particles, instance);
				}

				anims.remove(i);
				continue;
			}
			break;
		case ANIM_FIELD_OF_VISION_CHANGED:
			if (anim->field_of_vision.buffer_id
			 && anim->field_of_vision.start_time
			  + anim->field_of_vision.duration <= dyn_time) {
				fov_render_set_alpha(&draw->fov_render,
				                     anim->field_of_vision.buffer_id,
				                     1.0f);
				anims.remove(i);
				continue;
			} else if (!anim->field_of_vision.buffer_id
			        && anim->field_of_vision.start_time <= dyn_time) {
				u32 buffer_id = fov_render_add_fov(&draw->fov_render,
				                                   anim->field_of_vision.fov);
				anim->field_of_vision.buffer_id = buffer_id;
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
		case ANIM_PROJECTILE_EFFECT_24: {
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
			instance.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
			instance.sprite_pos = anim->projectile.sprite_coords;

			sprite_sheet_instances_add(&draw->effects_24, instance);
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
			instance.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
			instance.sprite_pos = anim->projectile.sprite_coords;

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
		case ANIM_FIELD_OF_VISION_CHANGED: {
			u32 buffer_id = anim->field_of_vision.buffer_id;
			f32 start_time = anim->field_of_vision.start_time;
			f32 duration = anim->field_of_vision.duration;
			f32 dt = (dyn_time - start_time) / duration;
			if (buffer_id && 0 < dt && dt <= 1.0f) {
				fov_render_set_alpha(&draw->fov_render, buffer_id, dt);
			}
			break;
		}
		}
	}

	if (camera_offset_mag > 0.0f) {
		f32 theta = uniform_f32(0.0f, 2.0f * PI_F32);
		m2 m = m2::rotation(theta);
		v2 v = m * v2(camera_offset_mag, 0.0f);
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
	v4 color_mod;
	u32 card_id;
	v2 card_face;
};

enum Card_Anim_State_Type
{
	CARD_ANIM_STATE_NORMAL,
	CARD_ANIM_STATE_NORMAL_TO_SELECTED,
	CARD_ANIM_STATE_SELECTED,
	CARD_ANIM_STATE_SELECTED_TO_NORMAL,
};

#define MAX_CARD_ANIMS 1024
struct Card_Anim_State
{
	Card_Anim_State_Type type;
	Hand_Params          hand_params;
	u32                  hand_size;
	u32                  highlighted_card_id;
	u32                  num_card_anims;
	Card_Anim            card_anims[MAX_CARD_ANIMS];

	union {
		struct {
			f32 start_time;
			f32 duration;
			u32 selected_card_id;
		} normal_to_selected;
		struct {
			u32 selected_card_id;
		} selected;
		struct {
			f32 start_time;
			f32 duration;
		} selected_to_normal;
	};
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

	Hand_Params hand_params = card_anim_state->hand_params;
	u32 highlighted_card_id = card_anim_state->highlighted_card_id;
	u32 num_card_anims = card_anim_state->num_card_anims;
	u32 selected_card_index = card_anim_state->hand_size;
	Card_Anim *anim = card_anim_state->card_anims;
	log(debug_log, "highlighted_card_id = %u", highlighted_card_id);
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
	log(debug_log, "num_card_anims: %u", num_card_anims);
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
		anim->color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
		switch (anim->type) {
		case CARD_ANIM_DECK:
			anim->pos.type = CARD_POS_DECK;
			break;
		case CARD_ANIM_DISCARD:
			anim->pos.type = CARD_POS_DISCARD;
			break;
		case CARD_ANIM_DRAW: {
			v2 start_pos = v2(-ratio + hand_params.border / 2.0f,
			                      -1.0f  + hand_params.height / 2.0f);
			f32 start_rotation = 0.0f;

			u32 hand_index = anim->draw.hand_index;
			f32 theta = hand_params.theta;
			f32 base_angle = PI_F32 / 2.0f + theta * (0.5f - (f32)hand_index * base_delta);

			f32 a = base_angle + deltas[hand_index];
			f32 r = hand_params.radius;

			v2 end_pos = r * v2(cosf(a), sinf(a)) + hand_params.center;
			f32 end_rotation = a - PI_F32 / 2.0f;

			f32 dt = (time - anim->draw.start_time) / anim->draw.duration;
			dt = clamp(dt, 0.0f, 1.0f);
			f32 jump = dt * (1.0f - dt) * 4.0f;
			f32 smooth = (3.0f - 2.0f * dt) * dt * dt;

			if (dt < 0.5f) {
				anim->pos.z_offset = 2.0f - (f32)anim->draw.hand_index / hand_params.num_cards;
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

			v2 start_pos = r2 * v2(cosf(a), sinf(a));
			start_pos.x += hand_params.center.x;
			start_pos.y += hand_params.top - r;
			f32 start_angle = a - PI_F32 / 2.0f;
			f32 start_zoom = z;

			v2 end_pos = v2(ratio - hand_params.border / 2.0f,
			                    -1.0f + 3.0f * hand_params.height / 2.0f);
			f32 end_angle = 0.0f;
			f32 end_zoom = 1.0f;

			f32 dt = (time - anim->hand_to_in_play.start_time) / anim->hand_to_in_play.duration;
			dt = clamp(dt, 0.0f, 1.0f);
			anim->pos.absolute.pos   = lerp(start_pos,   end_pos,   dt);
			anim->pos.absolute.angle = lerp(start_angle, end_angle, dt);
			anim->pos.absolute.zoom  = lerp(start_zoom,  end_zoom,  dt);
			break;
		}
		case CARD_ANIM_IN_PLAY:
			anim->pos.type = CARD_POS_ABSOLUTE;
			anim->pos.absolute.pos = v2(ratio - hand_params.border / 2.0f,
			                                -1.0f + 3.0f * hand_params.height / 2.0f);
			anim->pos.absolute.angle = 0.0f;
			anim->pos.absolute.zoom = 1.0f;
			break;
		case CARD_ANIM_HAND_TO_DISCARD: {
			anim->pos.type = CARD_POS_ABSOLUTE;

			f32 a = anim->hand_to_discard.start.angle;
			f32 r = anim->hand_to_discard.start.radius;
			f32 z = anim->hand_to_discard.start.zoom;
			f32 r2 = r + (z - 1.0f) * hand_params.card_size.h;

			v2 start_pos = r2 * v2(cosf(a), sinf(a));
			start_pos.x += hand_params.center.x;
			start_pos.y += hand_params.top - r;
			f32 start_angle = a - PI_F32 / 2.0f;
			f32 start_zoom = z;

			v2 end_pos = v2(ratio - hand_params.border / 2.0f,
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

			v2 end_pos = v2(ratio - hand_params.border / 2.0f,
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

	u32 highlighted_card_id = card_anim_state->highlighted_card_id;
	switch (card_anim_state->type) {
	case CARD_ANIM_STATE_SELECTED:
		highlighted_card_id = card_anim_state->selected.selected_card_id;
		break;
	case CARD_ANIM_STATE_NORMAL_TO_SELECTED:
		highlighted_card_id = card_anim_state->normal_to_selected.selected_card_id;
		break;
	}

	u8 do_hand_to_hand = 0;

	f32 next_draw_time = 0.0f;
	for (u32 i = 0; i < events.len; ++i) {
		Card_Event *event = &events[i];
		switch (event->type) {
		case CARD_EVENT_SELECT: {
			card_anim_state->type = CARD_ANIM_STATE_NORMAL_TO_SELECTED;
			card_anim_state->normal_to_selected.selected_card_id = event->select.card_id;
			card_anim_state->normal_to_selected.start_time = time;
			card_anim_state->normal_to_selected.duration
				= constants.cards_ui.normal_to_selected_duration;
			break;
		}
		case CARD_EVENT_UNSELECT: {
			card_anim_state->type = CARD_ANIM_STATE_SELECTED_TO_NORMAL;
			card_anim_state->selected_to_normal.start_time = time;
			card_anim_state->selected_to_normal.duration
				= constants.cards_ui.normal_to_selected_duration;
			break;
		}
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

	switch (card_anim_state->type) {
	case CARD_ANIM_STATE_SELECTED:
		highlighted_card_id = card_anim_state->selected.selected_card_id;
		break;
	case CARD_ANIM_STATE_NORMAL_TO_SELECTED:
		highlighted_card_id = card_anim_state->normal_to_selected.selected_card_id;
		break;
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

	// update state if necessary
	switch (card_anim_state->type) {
	case CARD_ANIM_STATE_NORMAL:
	case CARD_ANIM_STATE_SELECTED:
		break;
	case CARD_ANIM_STATE_NORMAL_TO_SELECTED:
		if (card_anim_state->normal_to_selected.start_time
		  + card_anim_state->normal_to_selected.duration <= time) {
			u32 card_id = card_anim_state->normal_to_selected.selected_card_id;
			card_anim_state->selected.selected_card_id = card_id;
			card_anim_state->type = CARD_ANIM_STATE_SELECTED;
		}
		break;
	case CARD_ANIM_STATE_SELECTED_TO_NORMAL:
		if (card_anim_state->selected_to_normal.start_time
		  + card_anim_state->selected_to_normal.duration <= time) {
			card_anim_state->type = CARD_ANIM_STATE_NORMAL;
		}
		break;
	}

	// convert finished card anims
	for (u32 i = 0; i < num_card_anims; ) {
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
				anim->type = CARD_ANIM_IN_PLAY;
			}
			break;
		case CARD_ANIM_IN_PLAY_TO_DISCARD:
			if (anim->in_play_to_discard.start_time
			  + anim->in_play_to_discard.duration <= time) {
				card_anim_state->card_anims[i]
					= card_anim_state->card_anims[--num_card_anims];
				continue;
			}
			break;
		case CARD_ANIM_HAND_TO_DISCARD:
			if (anim->hand_to_discard.start_time + anim->hand_to_discard.duration <= time) {
				card_anim_state->card_anims[i]
					= card_anim_state->card_anims[--num_card_anims];
				continue;
			}
			break;
		}
		++i;
	}
	card_anim_state->num_card_anims = num_card_anims;

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

	v4 hand_color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
	switch (card_anim_state->type) {
	case CARD_ANIM_STATE_NORMAL:
		break;
	case CARD_ANIM_STATE_SELECTED:
		hand_color_mod *= constants.cards_ui.selection_fade;
		break;
	case CARD_ANIM_STATE_NORMAL_TO_SELECTED: {
		f32 dt = (time - card_anim_state->normal_to_selected.start_time)
		         / card_anim_state->normal_to_selected.duration;
		dt = clamp(dt, 0.0f, 1.0f);
		hand_color_mod *= lerp(1.0f, constants.cards_ui.selection_fade, dt);
		break;
	}
	case CARD_ANIM_STATE_SELECTED_TO_NORMAL: {
		f32 dt = (time - card_anim_state->selected_to_normal.start_time)
		         / card_anim_state->selected_to_normal.duration;
		dt = clamp(dt, 0.0f, 1.0f);
		hand_color_mod *= lerp(constants.cards_ui.selection_fade, 1.0f, dt);
		break;
	}
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

			v2 pos = r2 * v2(cosf(a), sinf(a));
			pos.y += params->top - r;

			instance.screen_rotation = anim->pos.hand.angle - PI_F32 / 2.0f;
			instance.screen_pos = pos;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = anim->pos.z_offset;
			instance.zoom = z;
			instance.color_mod = anim->color_mod;
			if (anim->card_id != highlighted_card_id) {
				instance.color_mod = hand_color_mod;
			}
			break;
		}
		case CARD_POS_ABSOLUTE:
			instance.screen_rotation = anim->pos.absolute.angle;
			instance.screen_pos = anim->pos.absolute.pos;
			instance.card_pos = anim->card_face;
			instance.card_id = anim->card_id;
			instance.z_offset = anim->pos.z_offset;
			instance.zoom = anim->pos.absolute.zoom;
			instance.color_mod = anim->color_mod;
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
		instance.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
		card_render_add_instance(card_render, instance);
	}

	// add discard pile card
	if (card_state->discard) {
		Card card = card_state->discard[card_state->discard.len - 1];

		Card_Render_Instance instance = {};
		instance.screen_rotation = 0.0f;
		instance.screen_pos = { ratio - params->border / 2.0f,
		                        -1.0f + params->height / 2.0f };
		instance.card_pos = { 1.0f, 0.0f };
		instance.zoom = 1.0f;
		instance.card_id = discard_id;
		instance.z_offset = 0.0f;
		instance.card_pos = card_appearance_get_sprite_coords(card.appearance);
		instance.color_mod = v4(1.0f, 1.0f, 1.0f, 1.0f);
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
	Log                 debug_pause_log;

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
	debug_pause_log = &program->debug_pause_log;

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

void build_level_lich(Program *program)
{
	game_build_from_string(&program->game,
		"##############################\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#............................#\n"
		"#.............L..............#\n"
		"#............................#\n"
		"#........S........S..........#\n"
		"#............................#\n"
		"#............................#\n"
		"#......S......@......S.......#\n"
		"#............................#\n"
		"#............................#\n"
		"#........S.........S.........#\n"
		"#.............S..............#\n"
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

void build_field_of_vision_test(Program *program)
{
	game_build_from_string(&program->game,
		"##############################\n"
		"#............................#\n"
		"#............................#\n"
		"#.................#.#.#......#\n"
		"#............................#\n"
		"#.............#...#...#...#..#\n"
		"#............................#\n"
		"#........#....#...#.#.#.#.#..#\n"
		"#..................#.........#\n"
		"#................#..#........#\n"
		"#......#......@......#.......#\n"
		"#............................#\n"
		"#............................#\n"
		"#........#.........#.........#\n"
		"#.............#.....#..#.....#\n"
		"#............................#\n"
		"#................#...#.#.....#\n"
		"#............................#\n"
		"#................#..#........#\n"
		"#............................#\n"
		"#............................#\n"
		"##############################\n");

	program->draw.camera.zoom = 14.0f;
	Pos player_pos = game_get_player_pos(&program->game);
	program->draw.camera.world_center = (v2)player_pos;

	memset(&program->world_anim, 0, sizeof(program->world_anim));
	world_anim_init(&program->world_anim, &program->game);
}

void build_deck_random_n(Program *program, u32 n)
{
	// cards
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->card_anim_state;
	memset(card_state, 0, sizeof(*card_state));
	memset(card_anim_state, 0, sizeof(*card_anim_state));

	for (u32 i = 0; i < n; ++i) {
		Card card = {};
		card.id = i + 1;
		card.appearance = (Card_Appearance)(rand_u32() % NUM_CARD_APPEARANCES);
		card_state->discard.append(card);
	}
}

void build_lightning_deck(Program *program)
{
	Card_State *card_state = &program->game.card_state;
	Card_Anim_State *card_anim_state = &program->card_anim_state;
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
	fov_render_init(&program->draw.fov_render, { 256, 256 });


	program->draw.tiles.data         = SPRITE_SHEET_TILES;
	program->draw.creatures.data     = SPRITE_SHEET_CREATURES;
	program->draw.water_edges.data   = SPRITE_SHEET_WATER_EDGES;
	program->draw.effects_24.data    = SPRITE_SHEET_EFFECTS_24;
	program->draw.effects_32.data    = SPRITE_SHEET_EFFECTS_32;
	program->draw.boxy_bold.tex_size = boxy_bold_texture_size;
	program->draw.boxy_bold.tex_data = boxy_bold_pixel_data;

	build_level_default(program);
	build_deck_random_n(program, 100);

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
	start_thread(load_assets, program);
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

	if (!sprite_sheet_instances_d3d11_init(&program->draw.effects_24, device)) {
		goto error_init_sprite_sheet_effects_24;
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

	if (!fov_render_d3d11_init(&program->draw.fov_render, device)) {
		goto error_init_fov_render;
	}

	program->max_screen_size      = screen_size;
	program->d3d11.output_texture = output_texture;
	program->d3d11.output_uav     = output_uav;
	program->d3d11.output_rtv     = output_rtv;
	program->d3d11.output_srv     = output_srv;
	program->d3d11.output_vs      = output_vs;
	program->d3d11.output_ps      = output_ps;

	return 1;

	fov_render_d3d11_free(&program->draw.fov_render);
error_init_fov_render:
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
	sprite_sheet_instances_d3d11_free(&program->draw.effects_24);
error_init_sprite_sheet_effects_24:
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
	fov_render_d3d11_free(&program->draw.fov_render);
	debug_draw_world_d3d11_free(&program->debug_draw_world);
	debug_line_d3d11_free(&program->draw.card_debug_line);
	card_render_d3d11_free(&program->draw.card_render);
	imgui_d3d11_free(&program->imgui);
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
	sprite_sheet_font_instances_d3d11_free(&program->draw.boxy_bold);
	sprite_sheet_instances_d3d11_free(&program->draw.effects_24);
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
	program->draw.renderer.time = time;
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

	// XXX - check player can see sprite
	{
		Entity_ID player_id = program->game.controllers[0].player.entity_id;
		Entity *target = game_get_entity_by_id(&program->game, sprite_id);
		if (target) {
			if (!calculate_line_of_sight(&program->game, player_id, target->pos)) {
				sprite_id = 0;
			}
		}
	}

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
			case CARD_APPEARANCE_FIRE_BOLT: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_SELECT;
				card_event.select.card_id = card_id;
				card_state->events.append(card_event);

				Controller *c = &program->game.controllers[0];
				ASSERT(c->type == CONTROLLER_PLAYER);

				program->action_being_built.type = ACTION_FIRE_BOLT;
				program->action_being_built.fire_bolt.caster_id = c->player.entity_id;

				Card_Param param = {};
				param.type = CARD_PARAM_CREATURE;
				param.creature.id = &program->action_being_built.fire_bolt.target_id;
				program->card_params_stack.push(param);
				program->program_input_state_stack.push(GIS_CARD_PARAMS);

				break;
			}
			case CARD_APPEARANCE_FIRE_MANA: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_HAND_TO_IN_PLAY;
				card_event.hand_to_in_play.card_id = card_id;
				card_state->events.append(card_event);

				card_state->in_play.append(*card);
				card_state->hand.remove_preserve_order(hand_index);
				break;
			}
			case CARD_APPEARANCE_FIREBALL: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_SELECT;
				card_event.select.card_id = card_id;
				card_state->events.append(card_event);

				// card_state->in_play.append(*card);
				// card_state->hand.remove_preserve_order(hand_index);

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
			case CARD_APPEARANCE_LIGHTNING: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_SELECT;
				card_event.select.card_id = card_id;
				card_state->events.append(card_event);

				Entity *player = game_get_entity_by_id(&program->game, ENTITY_ID_PLAYER);
				ASSERT(player);

				program->action_being_built.type = ACTION_LIGHTNING;
				program->action_being_built.lightning.caster_id = ENTITY_ID_PLAYER;
				program->action_being_built.lightning.start = player->pos;

				Card_Param param = {};
				param.type = CARD_PARAM_TARGET;
				param.target.dest = &program->action_being_built.lightning.end;
				program->card_params_stack.push(param);
				program->program_input_state_stack.push(GIS_CARD_PARAMS);

				break;
			}
			case CARD_APPEARANCE_EXCHANGE: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_SELECT;
				card_event.select.card_id = card_id;
				card_state->events.append(card_event);

				// card_state->in_play.append(*card);
				// card_state->hand.remove_preserve_order(hand_index);

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
				card_event.type = CARD_EVENT_SELECT;
				card_event.select.card_id = card_id;
				card_state->events.append(card_event);

				// card_state->in_play.append(*card);
				// card_state->hand.remove_preserve_order(hand_index);

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
			Card_Anim_State *card_anim_state = &program->card_anim_state;
			ASSERT(card_anim_state->type == CARD_ANIM_STATE_SELECTED
			    || card_anim_state->type == CARD_ANIM_STATE_NORMAL_TO_SELECTED);

			Card_State *card_state = &program->game.card_state;
			Card_Event event = {};
			event.type = CARD_EVENT_UNSELECT;
			card_state->events.append(event);

			// XXX
			u32 card_id = 0;
			switch (card_anim_state->type) {
			case CARD_ANIM_STATE_SELECTED:
				card_id = card_anim_state->selected.selected_card_id;
				break;
			case CARD_ANIM_STATE_NORMAL_TO_SELECTED:
				card_id = card_anim_state->normal_to_selected.selected_card_id;
				break;
			}
			ASSERT(card_id);
			event.type = CARD_EVENT_HAND_TO_IN_PLAY;
			event.hand_to_in_play.card_id = card_id;
			card_state->events.append(event);

			Card *card = NULL;
			u32 hand_index;
			for (u32 i = 0; i < card_state->hand.len; ++i) {
				if (card_state->hand[i].id == card_id) {
					card = &card_state->hand[i];
					hand_index = i;
					break;
				}
			}
			ASSERT(card);
			card_state->in_play.append(*card);
			card_state->hand.remove_preserve_order(hand_index);

			// XXX
			card_anim_update_anims(card_anim_state, card_state->events, time);

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

				sprite_sheet_font_instances_add(&program->draw.boxy_bold, instance);

				instance.world_offset.x += (f32)glyph.dimensions.w - 1.0f;
			}
		}
	}

	// imgui
	IMGUI_Context *ic = &program->imgui;
	imgui_begin(ic, input, screen_size);
	if (program->display_debug_ui) {
		imgui_set_text_cursor(ic, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
		if (imgui_tree_begin(ic, "show mouse info")) {
			imgui_text(ic, "Mouse Pos: (%u, %u)", input->mouse_pos.x, input->mouse_pos.y);
			imgui_text(ic, "Mouse Delta: (%d, %d)",
			           input->mouse_delta.x, input->mouse_delta.y);
			imgui_text(ic, "Mouse world pos: (%d, %d)", world_mouse_pos.x, world_mouse_pos.y);
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
			imgui_text(ic, "Sprite ID: %u, position: (%u, %u)", sprite_id,
			           sprite_pos.x, sprite_pos.y);
			imgui_text(ic, "Card mouse pos: (%f, %f)", card_mouse_pos.x, card_mouse_pos.y);
			imgui_text(ic, "Selected Card ID: %u", selected_card_id);
			imgui_text(ic, "World Center: (%f, %f)", program->draw.camera.world_center.x,
			           program->draw.camera.world_center.y);
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
		if (imgui_tree_begin(ic, "levels")) {
			if (imgui_button(ic, "default")) {
				build_level_default(program);
			}
			if (imgui_button(ic, "anim test")) {
				build_level_anim_test(program);
			}
			if (imgui_button(ic, "slime test")) {
				build_level_slime_test(program);
			}
			if (imgui_button(ic, "lich test")) {
				build_level_lich(program);
			}
			if (imgui_button(ic, "field of vision test")) {
				build_field_of_vision_test(program);
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
		if (imgui_tree_begin(ic, "debug log")) {
			u32 start = 0;
			u32 end = debug_log->cur_line;
			if (end > LOG_MAX_LINES) {
				start = end - LOG_MAX_LINES;
			}

			for (u32 i = start; i < end; ++i) {
				imgui_text(ic, log_get_line(debug_log, i));
			}
			imgui_tree_end(ic);
		}
		imgui_tree_end(ic);
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
			// draw log
			{
				u32 start = 0;
				u32 end = program->debug_pause_log.cur_line;
				if (end > LOG_MAX_LINES) {
					start = end - LOG_MAX_LINES;
				}

				for (u32 i = start; i < end; ++i) {
					imgui_text(&program->imgui,
					           log_get_line(&program->debug_pause_log, i));
				}
			}
		}
		break;
	}
}

void render_d3d11(Program* program, ID3D11DeviceContext* dc, ID3D11RenderTargetView* output_rtv)
{
	v2_u32 screen_size_u32 = (v2_u32)program->screen_size;

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(program->d3d11.output_rtv, clear_color);

	fov_render_d3d11_draw(&program->draw.fov_render, dc);
	sprite_sheet_renderer_d3d11_begin(&program->draw.renderer, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.renderer, &program->draw.tiles, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.renderer, &program->draw.creatures, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.renderer, &program->draw.water_edges, dc);
	sprite_sheet_renderer_d3d11_do_particles(&program->draw.renderer, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.renderer, &program->draw.effects_24, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.renderer, &program->draw.effects_32, dc);
	sprite_sheet_renderer_d3d11_highlight_sprite(&program->draw.renderer, dc);
	sprite_sheet_renderer_d3d11_begin_font(&program->draw.renderer, dc);
	sprite_sheet_font_instances_d3d11_draw(&program->draw.renderer, &program->draw.boxy_bold, dc);
	sprite_sheet_renderer_d3d11_end(&program->draw.renderer, dc);
	fov_render_d3d11_composite(&program->draw.fov_render,
	                           dc,
	                           program->draw.renderer.d3d11.output_uav,
	                           (v2_u32)program->draw.renderer.size);

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
	                            program->d3d11.output_rtv,
	                            program->draw.camera.world_center + program->draw.camera.offset,
	                            debug_zoom);

	imgui_d3d11_draw(&program->imgui, dc, program->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(program->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(program->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &program->d3d11.output_srv);
	dc->Draw(6, 0);
}
