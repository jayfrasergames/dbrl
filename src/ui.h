#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "types.h"

#include "game.h"
#include "sound.h"
#include "fov.h"

#include "draw.h"

// =============================================================================
// World anims
// =============================================================================

enum World_Anim_Modifier_Type
{
	ANIM_MOD_COLOR,
	ANIM_MOD_POS,
};

struct World_Anim_Modifier
{
	World_Anim_Modifier_Type type;
	union {
		struct {
			v4 color;
		} color;
		struct {
			v2  pos;
			f32 y_offset;
		} pos;
	};
};

enum World_Static_Anim_Type
{
	ANIM_TILE_STATIC,
	ANIM_TILE_LIQUID,
	ANIM_WATER_EDGE,
	ANIM_CREATURE_IDLE,
	ANIM_ITEM_WITH_SHADOW,
};

#define MAX_ANIM_MODIFIERS 16

struct World_Static_Anim
{
	World_Static_Anim_Type type;
	union {
		struct {
			f32 duration;
			f32 offset;
		} idle;
		struct {
			f32 offset;
			f32 duration;
			v2  second_sprite_coords;
		} tile_liquid;
		struct {
			v4_u8 color;
		} water_edge;
		struct {
			v2 shadow_sprite_coords;
		} item_with_shadow;
	};
	v2        sprite_coords;
	v2        world_coords;
	Entity_ID entity_id;
	// XXX -- should this really be here?
	// can't we work out the depth offset from the anim type?
	f32       depth_offset;

	Max_Length_Array<World_Anim_Modifier, MAX_ANIM_MODIFIERS> modifiers;
};

enum World_Anim_Dynamic_Type
{
	ANIM_ADD_ITEM,
	ANIM_ADD_CREATURE,
	ANIM_MOVE,
	ANIM_MOVE_BLOCKED,
	ANIM_DROP_TILE,
	ANIM_PROJECTILE_EFFECT_24,
	ANIM_PROJECTILE_EFFECT_32,
	ANIM_TEXT,
	ANIM_CAMERA_SHAKE,
	ANIM_DEATH,
	ANIM_EXCHANGE,
	ANIM_EXCHANGE_PARTICLES,
	ANIM_BLINK,
	ANIM_BLINK_PARTICLES,
	ANIM_SLIME_SPLIT,
	ANIM_POLYMORPH,
	ANIM_POLYMORPH_PARTICLES,
	ANIM_HEAL_PARTICLES,
	ANIM_FIELD_OF_VISION_CHANGED,
	ANIM_SHOOT_WEB_PARTICLES,
	ANIM_CREATURE_DROP_IN,
	ANIM_TURN_INVISIBLE,
	ANIM_TURN_VISIBLE,
	ANIM_OPEN_DOOR,
	ANIM_CLOSE_DOOR,
	ANIM_MAGIC_MISSILE,
};

struct World_Anim_Dynamic
{
	World_Anim_Dynamic_Type type;
	bool started;
	f32 start_time;
	f32 duration;
	union {
		struct {
			Entity_ID entity_id;
			v2        start;
			v2        end;
		} move;
		struct {
			Entity_ID entity_id;
		} drop_tile;
		struct {
			v2 start;
			v2 end;
			v2 sprite_coords;
		} projectile;
		struct {
			v2 pos;
			u8 caption[16];
			v4 color;
		} text;
		struct {
			f32 power;
		} camera_shake;
		struct {
			Entity_ID entity_id;
		} death;
		struct {
			Entity_ID a;
			Entity_ID b;
			v2        pos_a;
			v2        pos_b;
		} exchange;
		struct {
			v2 pos;
		} exchange_particles;
		struct {
			Entity_ID entity_id;
			v2        target;
		} blink;
		struct {
			v2  pos;
		} blink_particles;
		struct {
			Entity_ID new_id;
			v2        start;
			v2        end;
			v2        sprite_coords;
		} slime_split;
		struct {
			Entity_ID entity_id;
			v2        pos;
			v2        new_sprite_coords;
		} polymorph;
		struct {
			v2  pos;
		} polymorph_particles;
		struct {
			v2 start;
			v2 end;
		} heal_particles;
		struct {
			u32 buffer_id;
			Field_Of_Vision* fov;
		} field_of_vision;
		struct {
			v2  start;
			v2  end;
		} shoot_web_particles;
		struct {
			Entity_ID entity_id;
			v2        sprite_coords;
			v2        world_coords;
		} creature_drop_in;
		struct {
			Entity_ID entity_id;
		} turn_invisible;
		struct {
			Entity_ID entity_id;
		} turn_visible;
		struct {
			Entity_ID entity_id;
			v2        pos;
			v2        sprite_coords;
		} add_item;
		struct {
			Entity_ID entity_id;
			v2        pos;
			v2        sprite_coords;
		} add_creature;
		struct {
			Entity_ID entity_id;
			v2        sprite_coords;
		} open_door;
		struct {
			Entity_ID entity_id;
			v2        sprite_coords;
		} close_door;
		struct {
			v2 start;
			v2 end;
			v2 offset;
		} magic_missile;
	};
};

// =============================================================================
// Sound anims
// =============================================================================

struct Sound_Anim
{
	f32      start_time;
	Sound_ID sound_id;
};

// =============================================================================
// Card anims
// =============================================================================

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

enum Card_Movement_Anim_Type
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
	CARD_ANIM_ADD_CARD_TO_DISCARD,
};

struct Card_Anim
{
	Card_Movement_Anim_Type type;
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
		struct {
			f32 start_time;
			f32 duration;
			Card_Hand_Pos start;
		} hand_to_in_play;
		struct {
			f32           start_time;
			f32           duration;
			Card_Hand_Pos start;
			u32           discard_index;
		} hand_to_discard;
		struct {
			f32      start_time;
			f32      duration;
			Card_Pos start;
			u32      discard_index;
		} in_play_to_discard;
		struct {
			f32      start_time;
			f32      duration;
		} add_to_discard;
		struct {
			u32 index;
		} discard;
	};
	Card_Pos pos;
	v4 color_mod;
	u32 card_id;
	v2 card_face;
};

enum Card_Dynamic_Anim_Type
{
	CARD_ANIM_DISCARD_TO_DECK,
	CARD_ANIM_DISCARD_HAND,
	CARD_ANIM_PLAY_CARD,
};

struct Card_Dynamic_Anim
{
	Card_Dynamic_Anim_Type type;
	f32 start_time;
	f32 duration;
	bool started;
	union {
		struct {
			Card_ID card_id;
		} play_card;
	};
};

enum Card_Anim_Modifier_Type
{
	CARD_ANIM_MOD_FLASH,
};

struct Card_Anim_Modifier
{
	Card_Anim_Modifier_Type type;
	Card_ID card_id;
	union {
		struct {
			v4  color;
			f32 start_time;
			f32 duration;
			f32 flash_duration;
		} flash;
	};
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
	Card_Anim_State_Type                                 type;
	Hand_Params                                          hand_params;
	u32                                                  hand_size;
	u32                                                  highlighted_card_id;
	u32                                                  prev_highlighted_card_id;
	Max_Length_Array<Card_Anim, MAX_CARD_ANIMS>          card_anims;
	Max_Length_Array<Card_Anim_Modifier, MAX_CARD_ANIMS> card_anim_modifiers;
	Max_Length_Array<Card_Dynamic_Anim, MAX_CARD_ANIMS>  card_dynamic_anims;

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

	// XXX -- get rid of this long term
	// move to using dyn_start_time from anim_state
	f32 dyn_time_start;
};

// =============================================================================
// Anim state
// =============================================================================

#define MAX_WORLD_STATIC_ANIMS   (5*MAX_ENTITIES)
#define MAX_WORLD_DYNAMIC_ANIMS  (5*MAX_ENTITIES)
#define MAX_WORLD_MODIFIER_ANIMS (5*MAX_ENTITIES)
#define MAX_SOUND_ANIMS          1024

struct Anim_State
{
	f32    dyn_time_start;
	v2     camera_offset;
	Camera camera;

	Max_Length_Array<World_Static_Anim, MAX_WORLD_STATIC_ANIMS>     world_static_anims;
	Max_Length_Array<World_Anim_Dynamic, MAX_WORLD_DYNAMIC_ANIMS>   world_dynamic_anims;
	// Max_Length_Array<World_Anim_Modifier, MAX_WORLD_MODIFIER_ANIMS> world_modifier_anims;
	Max_Length_Array<Sound_Anim, MAX_SOUND_ANIMS>                   sound_anims;

	// TODO -- simplify this/put directly into Anim_State
	Card_Anim_State card_anim_state;

	// XXX -- temporary
	Draw *draw;
	Card_Render *card_render;
};


// =============================================================================
// UI State
// =============================================================================

enum UI_Mouse_Over_Type
{
	UI_MOUSE_OVER_NOTHING,
	UI_MOUSE_OVER_ENTITY,
	UI_MOUSE_OVER_CARD,
	UI_MOUSE_OVER_DECK,
	UI_MOUSE_OVER_DISCARD,
};

struct UI_Mouse_Over
{
	UI_Mouse_Over_Type type;
	union {
		struct {
			Entity_ID entity_id;
		} entity;
		struct {
			Card_ID card_id;
		} card;
	};
	v2  world_pos_pixels;
	Pos world_pos;
};

struct Entity_Highlight
{
	Entity_ID entity_id;
	v4        color;
};

void highlight_card(Card_Anim_State* card_anim_state, Card_ID card_id);
void select_card(Card_Anim_State* card_anim_state, Card_ID card_id);
void unselect_card(Card_Anim_State* card_anim_state);
void highlight_entity(Anim_State* anim_state, Entity_ID entity_id, v4 color);
void highlight_pos(Anim_State* anim_state, Pos pos, v4 color);

void init(Anim_State* anim_state, Game* game);
void build_animations(Anim_State* anim_state, Slice<Event> events, f32 time);
void draw(Anim_State* anim_state, Render* render, Sound_Player* sound_player, v2_u32 screen_size, f32 time);
// void highlight_entities(Anim_State* anim_state, Render* render, Slice<Entity_Highlight> highlights);
bool is_animating(Anim_State* anim_state);
UI_Mouse_Over get_mouse_over(Anim_State* anim_state, v2_u32 mouse_pos, v2_u32 screen_size);