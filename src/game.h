#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "types.h"

#include "fov.h"
#include "appearance.h"
#include "assets.h"

#define JFG_HEADER_ONLY
#include "gen/cards.data.h"
#undef JFG_HEADER_ONLY

// =============================================================================
// Forwards
// =============================================================================

struct Controller;
struct Game;

typedef u32 Card_ID;

// =============================================================================
// Card params
// =============================================================================

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
	size_t          offset;
	union {
		struct {
			Pos dest;
		} target;
		struct {
			Entity_ID id;
		} creature;
		struct {
			Pos       dest;
		} available_tile;
	};
};

// =============================================================================
// Actions
// =============================================================================

enum Action_Type
{
	ACTION_NONE,
	ACTION_MOVE,
	ACTION_WAIT,
	ACTION_BUMP_ATTACK,
	ACTION_BUMP_ATTACK_POISON,
	ACTION_BUMP_ATTACK_SNEAK,
	ACTION_FIREBALL,
	ACTION_FIRE_BOLT,
	ACTION_EXCHANGE,
	ACTION_BLINK,
	// ACTION_POISON,
	ACTION_HEAL,
	ACTION_LIGHTNING,
	ACTION_OPEN_DOOR,
	ACTION_CLOSE_DOOR,
	ACTION_SHOOT_WEB,
	ACTION_TURN_INVISIBLE,
	ACTION_STEAL_CARD,

	ACTION_DRAW_CARDS,
	ACTION_DISCARD_HAND,
	ACTION_PLAY_CARD,
};

struct Action
{
	Action_Type type;
	Entity_ID   entity_id;
	union {
		struct {
			Card_ID           card_id;
			Action_Type       action_type;
			Slice<Card_Param> params;
		} play_card;

		struct {
			Pos start;
			Pos end;
		} move;
		struct {
			Entity_ID target_id;
		} bump_attack;
		struct {
			Pos end;
		} fireball;
		struct {
			Entity_ID a;
			Entity_ID b;
		} exchange;
		struct {
			Pos       target;
		} blink;
		struct {
			Entity_ID target_id;
		} fire_bolt;
		struct {
			Entity_ID target_id;
			i32 amount;
		} heal;
		struct {
			Pos end;
		} lightning;
		struct {
			Entity_ID door_id;
		} open_door;
		struct {
			Entity_ID door_id;
		} close_door;
		struct {
			Pos target;
		} shoot_web;
		struct {
			Entity_ID target_id;
		} steal_card;
	};

	operator bool() { return type; }
};

// =============================================================================
// Controllers
// =============================================================================

typedef u32 Controller_ID;

enum Controller_Type
{
	CONTROLLER_PLAYER,
	CONTROLLER_RANDOM_MOVE,
	CONTROLLER_DRAGON,
	CONTROLLER_SLIME,
	CONTROLLER_LICH,
	CONTROLLER_IMP,

	CONTROLLER_SPIDER_NORMAL,
	CONTROLLER_SPIDER_WEB,
	CONTROLLER_SPIDER_POISON,
	CONTROLLER_SPIDER_SHADOW,
};

#define CONTROLLER_LICH_MAX_SKELETONS 16

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
		struct {
			Entity_ID imp_id;
		} imp;
		struct {
			Entity_ID entity_id;
		} spider_normal;
		struct {
			Entity_ID entity_id;
			u32       web_cooldown;
		} spider_web;
		struct {
			Entity_ID entity_id;
		} spider_poison;
		struct {
			Entity_ID entity_id;
			u32       invisible_cooldown;
		} spider_shadow;
	};
};

struct Potential_Move
{
	Entity_ID entity_id;
	Pos start;
	Pos end;
	f32 weight;
};

void make_bump_attacks(Controller* controller, Game* game, Output_Buffer<Action> attacks);
void make_moves(Controller* controller, Game* game, Output_Buffer<Potential_Move> moves);
void make_actions(Controller* controller, Game* game, Slice<bool> has_acted, Output_Buffer<Action> actions);

// =============================================================================
// Entities
// =============================================================================

// XXX - bit ugly, no?
enum Static_Entity_ID
{
	ENTITY_ID_NONE,
	ENTITY_ID_PLAYER,
	ENTITY_ID_WALLS,
	NUM_STATIC_ENTITY_IDS,
};

enum Block_Flag
{
	BLOCK_WALK = 1 << 0,
	BLOCK_SWIM = 1 << 1,
	BLOCK_FLY  = 1 << 2,
};

enum Entity_Flag
{
	ENTITY_FLAG_WALK_THROUGH_WEBS = 1 << 0,
	ENTITY_FLAG_INVISIBLE         = 1 << 1,
	ENTITY_FLAG_BLOCKS_VISION     = 1 << 2,
};

struct Entity
{
	Entity_ID     id;
	Entity_Flag   flags;
	u16           movement_type;
	u16           block_mask;
	Pos           pos;
	Appearance    appearance;
	i32           hit_points;
	i32           max_hit_points;
	Action_Type   default_action;
};

// =============================================================================
// Tiles
// =============================================================================

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

// =============================================================================
// Creatures
// =============================================================================

enum Creature_Type
{
	CREATURE_SPIDER_NORMAL,
	CREATURE_SPIDER_WEB,
	CREATURE_SPIDER_POISON,
	CREATURE_SPIDER_SHADOW,
	CREATURE_IMP,
};

// =============================================================================
// Messages
// =============================================================================

enum Message_Type : u32
{
	MESSAGE_MOVE_PRE_EXIT    = 1 << 0,
	MESSAGE_MOVE_POST_EXIT   = 1 << 1,
	MESSAGE_MOVE_PRE_ENTER   = 1 << 2,
	MESSAGE_MOVE_POST_ENTER  = 1 << 3,
	MESSAGE_DAMAGE           = 1 << 4,
	MESSAGE_PRE_DEATH        = 1 << 5,
	MESSAGE_POST_DEATH       = 1 << 6,
	MESSAGE_DRAW_CARD        = 1 << 7,
	MESSAGE_DISCARD_CARD_PRE = 1 << 8,
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
			Pos pos;
			Entity_ID entity_id;
		} death;
		struct {
			Card_ID card_id;
		} draw_card;
		struct {
			Card_ID card_id;
		} discard_card;
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
	MESSAGE_HANDLER_TRAP_SPIDER_CAVE,
	MESSAGE_HANDLER_SPIDER_WEB_PREVENT_EXIT,
	MESSAGE_HANDLER_EXPLODE_ON_DEATH,
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
		struct {
			Pos center;
			u32 radius;
			u32 last_dist_squared;
		} trap_spider_cave;
	};
};

// =============================================================================
// Cards
// =============================================================================

struct Card
{
	Card_ID         id;
	Card_Appearance appearance;
};

#define CARD_STATE_MAX_CARDS  1024

struct Card_State
{
	u32                                           hand_size;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>  deck;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>  discard;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>  hand;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>  in_play;
};

// =============================================================================
// Events
// =============================================================================

enum Event_Type
{
	EVENT_NONE,
	EVENT_MOVE,
	EVENT_MOVE_BLOCKED,
	EVENT_BUMP_ATTACK,
	EVENT_OPEN_DOOR,
	EVENT_CLOSE_DOOR,
	EVENT_DROP_TILE,
	EVENT_FIREBALL_HIT,
	EVENT_FIREBALL_SHOT,
	EVENT_FIREBALL_OFFSHOOT,
	EVENT_FIREBALL_OFFSHOOT_2,
	EVENT_STUCK,
	EVENT_POISONED,
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
	EVENT_SHOOT_WEB_CAST,
	EVENT_SHOOT_WEB_HIT,
	EVENT_CREATURE_DROP_IN,
	EVENT_ADD_CREATURE,
	EVENT_ADD_CARD_TO_DISCARD,
	EVENT_CARD_POISON,
	EVENT_TURN_INVISIBLE,
	EVENT_TURN_VISIBLE,

	EVENT_DRAW_CARD,
	EVENT_SHUFFLE_DISCARD_TO_DECK,
	EVENT_DISCARD,
	EVENT_DISCARD_HAND,
	EVENT_PLAY_CARD,
	EVENT_REMOVE_CARD,

	// XXX
	EVENT_CARD_DRAW,
	EVENT_CARD_HAND_TO_IN_PLAY,
	EVENT_CARD_HAND_TO_DISCARD,
	EVENT_CARD_IN_PLAY_TO_DISCARD,
	EVENT_CARD_SELECT,
	EVENT_CARD_UNSELECT,
};

struct Event
{
	Event_Type type;
	f32 time;
	union {
		struct {
			Card_ID         card_id;
			u32             hand_index;
		} card_draw;
		struct {
			Card_ID card_id;
			u32     discard_index;
		} discard;
		struct {
			Card_ID card_id;
		} play_card;
		struct {
			Card_ID card_id;
			Pos     target_pos;
		} remove_card;


		struct {
			u32 card_id;
		} card_hand_to_in_play;
		struct {
			Card_ID card_id;
		} card_in_play_to_discard;
		struct {
			Card_ID card_id;
		} card_hand_to_discard;
		struct {
			Card_ID card_id;
		} card_select;

		struct {
			Entity_ID entity_id;
			Pos start, end;
		} move;
		struct {
			Entity_ID attacker_id;
			Pos       start;
			Pos       end;
			Sound_ID  sound;
		} bump_attack;
		struct {
			Entity_ID  door_id;
			Appearance new_appearance;
		} open_door;
		struct {
			Entity_ID  door_id;
			Appearance new_appearance;
		} close_door;
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
			v2        pos;
		} poisoned;
		struct {
			Entity_ID entity_id;
		} death;
		struct {
			Entity_ID a;
			Entity_ID b;
			Pos       a_pos;
			Pos       b_pos;
		} exchange;
		struct {
			Entity_ID caster_id;
			Pos       start;
			Pos       target;
		} blink;
		struct {
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
			Field_Of_Vision *fov;
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
		struct {
			Entity_ID caster_id;
			v2        start;
			v2        end;
		} shoot_web_cast;
		struct {
			Entity_ID  web_id;
			Appearance appearance;
			v2         pos;
		} shoot_web_hit;
		struct {
			Entity_ID  entity_id;
			Appearance appearance;
			v2         pos;
		} creature_drop_in;
		struct {
			Entity_ID  creature_id;
			Appearance appearance;
			v2         pos;
		} add_creature;
		struct {
			Entity_ID       entity_id;
			Card_ID         card_id;
			Card_Appearance appearance;
		} add_card_to_discard;
		struct {
			Card_ID card_id;
		} card_poison;
		struct {
			Entity_ID entity_id;
		} turn_invisible;
		struct {
			Entity_ID entity_id;
		} turn_visible;
	};
};

// =============================================================================
// Game
// =============================================================================

#define GAME_MAX_CONTROLLERS 1024
#define GAME_MAX_MESSAGE_HANDLERS 1024

struct Game
{
	Entity_ID     next_entity_id;
	Controller_ID next_controller_id;
	Card_ID       next_card_id;

	Entity_ID     player_id;

	Max_Length_Array<Entity, MAX_ENTITIES> entities;

	Map_Cache<Tile> tiles;

	Max_Length_Array<Controller, GAME_MAX_CONTROLLERS> controllers;

	Card_State card_state;

	Max_Length_Array<Message_Handler, GAME_MAX_MESSAGE_HANDLERS> handlers;

	Field_Of_Vision field_of_vision;
};

// =============================================================================
// Functions
// =============================================================================

void             init(Game* game);
void             update_fov(Game* game);

Entity*          get_player(Game* game);
Entity*          get_entity_by_id(Game* game, Entity_ID entity_id);
Entity*          add_entity(Game* game);
Controller*      add_controller(Game* game);
Message_Handler* add_message_handler(Game* game);

Card*            add_card(Game* game, Card_Appearance appearance);

Entity*          add_creature(Game* game, Pos pos, Creature_Type type);
Appearance       get_creature_appearance(Creature_Type type);

Entity*          add_spider_normal(Game* game, Pos pos);
Entity*          add_spider_web(Game* game, Pos pos);
Entity*          add_spider_poison(Game* game, Pos pos);
Entity*          add_spider_shadow(Game* game, Pos pos);
Entity*          add_imp(Game* game, Pos pos);
Entity*          add_spiderweb(Game* game, Pos pos);
Entity*          add_explosive_barrel(Game* game, Pos pos);
Message_Handler* add_trap_spider_cave(Game* game, Pos pos, u32 radius);

Entity_ID        add_slime(Game* game, Pos pos, u32 hit_points);

void             do_action(Game* game, Action action, Output_Buffer<Event> events);
void             get_card_params(Game* game, Card_ID card_id, Action_Type* action_type, Output_Buffer<Card_Param> card_params);

// These functions should probably become internal
void             game_do_turn(Game* game, Output_Buffer<Event> events);
bool             game_is_pos_opaque(Game* game, Pos pos);
bool             tile_is_passable(Tile tile, u16 move_mask);

bool is_pos_passable(Game* game, Pos pos, u16 move_mask);
Pos get_pos(Game* game, Entity_ID entity_id);