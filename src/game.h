#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "types.h"

#include "fov.h"
#include "appearance.h"

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
// Actions
// =============================================================================

enum Action_Type
{
	ACTION_NONE,
	ACTION_MOVE,
	ACTION_WAIT,
	ACTION_BUMP_ATTACK,
	ACTION_BUMP_ATTACK_POISON,
	ACTION_FIREBALL,
	ACTION_FIRE_BOLT,
	ACTION_EXCHANGE,
	ACTION_BLINK,
	ACTION_POISON,
	ACTION_HEAL,
	ACTION_LIGHTNING,
	ACTION_OPEN_DOOR,
	ACTION_CLOSE_DOOR,
	ACTION_SHOOT_WEB,
};

struct Action
{
	Action_Type type;
	Entity_ID   entity_id;
	union {
		struct {
			Pos start;
			Pos end;
		} move;
		struct {
			Entity_ID target_id;
		} bump_attack;
		struct {
			Pos start;
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
			Entity_ID target;
			Card_ID   card_id;
		} poison;
		struct {
			Entity_ID target_id;
		} fire_bolt;
		struct {
			Entity_ID target_id;
			i32 amount;
		} heal;
		struct {
			Pos start;
			Pos end;
		} lightning;
		struct {
			Entity_ID door_id;
		} open_door;
		struct {
			Pos target;
		} shoot_web;
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

struct Entity
{
	Entity_ID     id;
	u16           movement_type;
	u16           block_mask;
	Pos           pos;
	Appearance    appearance;
	i32           hit_points;
	i32           max_hit_points;
	bool          blocks_vision;
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
};

// =============================================================================
// Messages
// =============================================================================

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
	MESSAGE_HANDLER_TRAP_SPIDER_CAVE,
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
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        deck;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        discard;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        hand;
	Max_Length_Array<Card, CARD_STATE_MAX_CARDS>        in_play;
};

#define GAME_MAX_CONTROLLERS 1024
#define GAME_MAX_MESSAGE_HANDLERS 1024

// =============================================================================
// Game
// =============================================================================

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
Entity*          add_spiderweb(Game* game, Pos pos);
Message_Handler* add_trap_spider_cave(Game* game, Pos pos, u32 radius);

Entity_ID        add_slime(Game* game, Pos pos, u32 hit_points);

// These functions should probably become internal
Entity*          game_get_entity_by_id(Game* game, Entity_ID entity_id);
bool             game_is_pos_opaque(Game* game, Pos pos);
bool             tile_is_passable(Tile tile, u16 move_mask);

bool is_pos_passable(Game* game, Pos pos, u16 move_mask);