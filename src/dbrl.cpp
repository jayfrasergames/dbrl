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
#include "physics.h"
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
void debug_pause();

// =============================================================================
// game

#define GAME_MAX_ACTIONS 1024
typedef Max_Length_Array<Action, GAME_MAX_ACTIONS> Action_Buffer;

// Transactions
#define MAX_EVENTS 10240

// =============================================================================
// cards

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

// maybe better called "world state"?
Entity_ID game_new_entity_id(Game *game)
{
	return game->next_entity_id++;
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
		case CONTROLLER_SPIDER_NORMAL:
			if (c->spider_normal.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
		case CONTROLLER_SPIDER_WEB:
			if (c->spider_web.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
		case CONTROLLER_SPIDER_POISON:
			if (c->spider_poison.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
		case CONTROLLER_SPIDER_SHADOW:
			if (c->spider_shadow.entity_id == entity_id) {
				controllers.remove(i);
				goto break_loop;
			}
			break;
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

void game_draw_cards(Game* game, Action_Buffer* action_buffer, u32 num_to_draw, Output_Buffer<Card_Event> card_events)
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
		card_events.append(event);

		switch (card.appearance) {
		case CARD_APPEARANCE_POISON: {
			Action action = {};
			action.type = ACTION_POISON;
			Controller *c = &game->controllers[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			action.entity_id = c->player.entity_id;
			action.poison.target = c->player.entity_id;
			action.poison.card_id = card.id;
			actions.append(action);
			break;
		}
		}
	}
}

void game_end_play_cards(Game* game, Output_Buffer<Card_Event> card_events)
{
	Card_State *card_state = &game->card_state;

	while (card_state->in_play) {
		Card card = card_state->in_play.pop();
		card_state->discard.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_IN_PLAY_TO_DISCARD;
		event.in_play_to_discard.card_id = card.id;
		card_events.append(event);
	}

	auto& hand = card_state->hand;
	for (u32 i = 0; i < hand.len; ++i) {
		Card card = hand[i];
		card_state->discard.append(card);
		Card_Event event = {};
		event.type = CARD_EVENT_HAND_TO_DISCARD;
		event.hand_to_discard.card_id = card.id;
		card_events.append(event);
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
	TRANSACTION_BUMP_ATTACK,
	TRANSACTION_BUMP_ATTACK_CONNECT,
	TRANSACTION_BUMP_ATTACK_POISON,
	TRANSACTION_BUMP_ATTACK_POISON_CONNECT,
	TRANSACTION_BUMP_ATTACK_SNEAK,
	TRANSACTION_BUMP_ATTACK_SNEAK_CONNECT,
	TRANSACTION_OPEN_DOOR,
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
	TRANSACTION_SHOOT_WEB_CAST,
	TRANSACTION_SHOOT_WEB_HIT,
	TRANSACTION_CREATURE_DROP_IN,
	TRANSACTION_ADD_CREATURE,
	TRANSACTION_TURN_INVISIBLE_CAST,
	TRANSACTION_TURN_INVISIBLE,
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
			Entity_ID attacker_id;
			Entity_ID target_id;
			Pos start;
			Pos end;
		} bump_attack;
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
			Card_ID   card_id;
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
		struct {
			Entity_ID entity_id;
			Entity_ID door_id;
		} open_door;
		struct {
			Entity_ID caster_id;
			Pos       target;
		} shoot_web;
		struct {
			Pos           pos;
			Creature_Type type;
		} creature_drop_in;
		struct {
			Pos           pos;
			Creature_Type type;
		} add_creature;
		struct {
			Entity_ID entity_id;
		} turn_invisible;
	};
};

void game_dispatch_message(Game*                      game,
                           Message                    message,
                           f32                        time,
                           Output_Buffer<Transaction> transactions,
                           Output_Buffer<Event>       events,
                           void*                      data)
{
	auto &handlers = game->handlers;
	u32 num_handlers = handlers.len;
	for (u32 i = 0; i < handlers.len; ) {
		Message_Handler *h = &handlers[i];
		if (!(message.type & h->handle_mask)) {
			++i;
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
		case MESSAGE_HANDLER_SPIDER_WEB_PREVENT_EXIT: {
			if (h->prevent_exit.pos == message.move.start) {
				auto entity = game_get_entity_by_id(game, message.move.entity_id);
				if (!(entity->flags & ENTITY_FLAG_WALK_THROUGH_WEBS)) {
					u8 *can_exit = (u8*) data;
					*can_exit = 0;
				}
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
		case MESSAGE_HANDLER_TRAP_SPIDER_CAVE: {
			Pos end = message.move.end;
			v2_i32 trap_pos = (v2_i32)h->trap_spider_cave.center;
			i32 radius = h->trap_spider_cave.radius;
			v2_i32 d = (v2_i32)end - trap_pos;
			i32 dist_squared = (i32)(d.x*d.x + d.y*d.y);
			if (dist_squared <= radius*radius) {
				if (dist_squared <= h->trap_spider_cave.last_dist_squared) {
					h->trap_spider_cave.last_dist_squared = dist_squared;
					break;
				}

				Max_Length_Array<Pos, 1024> spawn_poss;
				spawn_poss.reset();

				for (i32 y = trap_pos.y - radius; y <= trap_pos.y + radius; ++y) {
					for (i32 x = trap_pos.x - radius; x <= trap_pos.x + radius; ++x) {
						Pos p = Pos(x, y);
						if (is_pos_passable(game, p, BLOCK_WALK)) {
							spawn_poss.append(p);
						}
					}
				}
				ASSERT(spawn_poss.len >= 5);

				for (u32 i = 0; i < 5; ++i) {
					u32 idx = i + rand_u32() % (spawn_poss.len - i);
					auto tmp = spawn_poss[idx];
					spawn_poss[idx] = spawn_poss[i];
					spawn_poss[i] = tmp;
				}

				Transaction t = {};
				t.type = TRANSACTION_CREATURE_DROP_IN;
				t.start_time = time + constants.anims.creature_drop_in.duration;

				t.creature_drop_in.pos = spawn_poss[0];
				t.creature_drop_in.type = CREATURE_SPIDER_NORMAL;
				transactions.append(t);

				t.creature_drop_in.pos = spawn_poss[1];
				t.creature_drop_in.type = CREATURE_SPIDER_NORMAL;
				transactions.append(t);

				t.creature_drop_in.pos = spawn_poss[2];
				t.creature_drop_in.type = CREATURE_SPIDER_WEB;
				transactions.append(t);

				t.creature_drop_in.pos = spawn_poss[3];
				t.creature_drop_in.type = CREATURE_SPIDER_POISON;
				transactions.append(t);

				t.creature_drop_in.pos = spawn_poss[4];
				t.creature_drop_in.type = CREATURE_SPIDER_SHADOW;
				transactions.append(t);

				handlers.remove(i);
				continue;
			}
			break;
		}
		} // end switch

		++i;
	}
}

// #define DEBUG_CREATE_MOVES
// #define DEBUG_SHOW_ACTIONS
// #define DEBUG_TRANSACTION_PROCESSING

void game_simulate_actions(Game* game, Slice<Action> actions, Output_Buffer<Event> events)
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
			if (entities[i].flags & ENTITY_FLAG_BLOCKS_VISION) {
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

	f32 bump_attack_delay = 0.0f;

	for (u32 i = 0; i < actions.len; ++i) {
		Action action = actions[i];
		switch (action.type) {
		case ACTION_BUMP_ATTACK:
		case ACTION_BUMP_ATTACK_POISON:
		case ACTION_BUMP_ATTACK_SNEAK:
			bump_attack_delay = constants.anims.bump_attack.duration / 2.0f;
			break;
		}
	}

	Max_Length_Array<Transaction, GAME_MAX_TRANSACTIONS> transactions;
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
			t.start_time = bump_attack_delay;
			t.move.entity_id = action.entity_id;
			t.move.start = action.move.start;
			t.move.end = action.move.end;
			transactions.append(t);
			break;
		}
		case ACTION_BUMP_ATTACK: {
			Transaction t = {};
			t.type = TRANSACTION_BUMP_ATTACK;
			t.start_time = 0.0f;
			t.bump_attack.attacker_id = action.entity_id;
			t.bump_attack.target_id = action.bump_attack.target_id;
			transactions.append(t);
			break;
		}
		case ACTION_BUMP_ATTACK_POISON: {
			Transaction t = {};
			t.type = TRANSACTION_BUMP_ATTACK_POISON;
			t.start_time = 0.0f;
			t.bump_attack.attacker_id = action.entity_id;
			t.bump_attack.target_id = action.bump_attack.target_id;
			transactions.append(t);
			break;
		}
		case ACTION_BUMP_ATTACK_SNEAK: {
			Transaction t = {};
			t.type = TRANSACTION_BUMP_ATTACK_SNEAK;
			t.start_time = 0.0f;
			t.bump_attack.attacker_id = action.entity_id;
			t.bump_attack.target_id = action.bump_attack.target_id;
			transactions.append(t);
			break;
		}
		case ACTION_OPEN_DOOR: {
			Transaction t = {};
			t.type = TRANSACTION_OPEN_DOOR;
			t.start_time = bump_attack_delay;
			t.open_door.entity_id = action.entity_id;
			t.open_door.door_id = action.open_door.door_id;
			transactions.append(t);
			break;
		}
		case ACTION_FIREBALL: {
			Transaction t = {};
			t.type = TRANSACTION_FIREBALL_SHOT;
			t.start_time = bump_attack_delay;
			t.fireball_shot.start = action.fireball.start;
			t.fireball_shot.end = action.fireball.end;
			transactions.append(t);
			break;
		}
		case ACTION_EXCHANGE: {
			Transaction t = {};
			t.type = TRANSACTION_EXCHANGE_CAST;
			t.start_time = bump_attack_delay;
			t.exchange.a = action.exchange.a;
			t.exchange.b = action.exchange.b;
			transactions.append(t);
			break;
		}
		case ACTION_BLINK: {
			Transaction t = {};
			t.type = TRANSACTION_BLINK_CAST;
			t.start_time = bump_attack_delay;
			t.blink.caster_id = action.entity_id;
			t.blink.target = action.blink.target;
			transactions.append(t);
			break;
		}
		case ACTION_POISON: {
			Transaction t = {};
			t.type = TRANSACTION_POISON_CAST;
			t.start_time = 0.0f;
			t.poison.entity_id = action.poison.target;
			t.poison.card_id = action.poison.card_id;
			transactions.append(t);
			break;
		}
		case ACTION_FIRE_BOLT: {
			Transaction t = {};
			t.type = TRANSACTION_FIRE_BOLT_CAST;
			t.start_time = bump_attack_delay;
			t.fire_bolt.caster_id = action.entity_id;
			t.fire_bolt.target_id = action.fire_bolt.target_id;
			transactions.append(t);
			break;
		}
		case ACTION_HEAL: {
			Transaction t = {};
			t.type = TRANSACTION_HEAL_CAST;
			t.start_time = bump_attack_delay;
			t.heal.caster_id = action.entity_id;
			t.heal.target_id = action.heal.target_id;
			t.heal.amount = action.heal.amount;
			transactions.append(t);
			break;
		}
		case ACTION_LIGHTNING: {
			Transaction t = {};
			t.type = TRANSACTION_LIGHTNING_CAST;
			t.start_time = bump_attack_delay;
			t.lightning.caster_id = action.entity_id;
			t.lightning.start = action.lightning.start;
			t.lightning.end = action.lightning.end;
			transactions.append(t);
			break;
		}
		case ACTION_SHOOT_WEB: {
			Transaction t = {};
			t.type = TRANSACTION_SHOOT_WEB_CAST;
			t.start_time = bump_attack_delay;
			t.shoot_web.caster_id = action.entity_id;
			t.shoot_web.target = action.shoot_web.target;
			transactions.append(t);
			break;
		}
		case ACTION_TURN_INVISIBLE: {
			Transaction t = {};
			t.type = TRANSACTION_TURN_INVISIBLE;
			t.start_time = bump_attack_delay;
			t.turn_invisible.entity_id = action.entity_id;
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

			// DPLOG("Time: %f, %x", time, *(u32*)&time);
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
				game_dispatch_message(game, m, time, transactions, events, &can_exit);

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
				game_dispatch_message(game, m, time, transactions, events, NULL);

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
				game_dispatch_message(game, m, time, transactions, events, &can_enter);

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
				game_dispatch_message(game, m, time, transactions, events, NULL);
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

			case TRANSACTION_BUMP_ATTACK:
			case TRANSACTION_BUMP_ATTACK_POISON:
			case TRANSACTION_BUMP_ATTACK_SNEAK: {
				// TODO -- move_pre_exit message
				//         move_post_exit message

				Entity *attacker = game_get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = game_get_entity_by_id(game, t->bump_attack.target_id);
				if (!attacker || !target) {
					t->type = TRANSACTION_REMOVE;
					break;
				}

				switch (t->type) {
				case TRANSACTION_BUMP_ATTACK:
					t->type = TRANSACTION_BUMP_ATTACK_CONNECT;
					break;
				case TRANSACTION_BUMP_ATTACK_POISON:
					t->type = TRANSACTION_BUMP_ATTACK_POISON_CONNECT;
					break;
				case TRANSACTION_BUMP_ATTACK_SNEAK:
					t->type = TRANSACTION_BUMP_ATTACK_SNEAK_CONNECT;
					break;
				default:
					ASSERT(0);
					break;
				}
				t->start_time += constants.anims.bump_attack.duration / 2.0f;
				t->bump_attack.start = attacker->pos;
				t->bump_attack.end = target->pos;

				break;
			}

			case TRANSACTION_BUMP_ATTACK_CONNECT: {
				// TODO -- move_pre_enter message
				//         move_post_enter message
				t->type = TRANSACTION_REMOVE;

				Entity *attacker = game_get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = game_get_entity_by_id(game, t->bump_attack.target_id);
				if (!attacker || !target) {
					break;
				}

				Entity_Damage damage = {};
				damage.entity_id = t->bump_attack.target_id;
				damage.damage = 1;
				damage.pos = (v2)t->bump_attack.end;
				entity_damage.append(damage);

				Event event = {};
				event.type = EVENT_BUMP_ATTACK;
				// XXX - yuck
				event.time = time - constants.anims.bump_attack.duration / 2.0f;
				event.bump_attack.attacker_id = t->bump_attack.attacker_id;
				event.bump_attack.start = t->bump_attack.start;
				event.bump_attack.end = t->bump_attack.end;
				Sound_ID sounds[] = { SOUND_PUNCH_1_1, SOUND_PUNCH_1_2, SOUND_PUNCH_2_1, SOUND_PUNCH_2_2 };
				u32 idx = rand_u32() % ARRAY_SIZE(sounds);
				event.bump_attack.sound = sounds[idx];
				events.append(event);

				break;
			}

			case TRANSACTION_BUMP_ATTACK_POISON_CONNECT: {
				// TODO -- move_pre_enter message
				//         move_post_enter message
				t->type = TRANSACTION_REMOVE;

				Entity *attacker = game_get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = game_get_entity_by_id(game, t->bump_attack.target_id);
				if (!attacker || !target) {
					break;
				}

				Entity_Damage damage = {};
				damage.entity_id = t->bump_attack.target_id;
				damage.damage = 1;
				damage.pos = (v2)t->bump_attack.end;
				entity_damage.append(damage);

				Event event = {};
				event.type = EVENT_BUMP_ATTACK;
				// XXX - yuck
				event.time = time - constants.anims.bump_attack.duration / 2.0f;
				event.bump_attack.attacker_id = t->bump_attack.attacker_id;
				event.bump_attack.start = t->bump_attack.start;
				event.bump_attack.end = t->bump_attack.end;
				event.bump_attack.sound = SOUND_STAB_1_2;
				events.append(event);

				event = {};
				event.type = EVENT_POISONED;
				event.time = time;
				event.poisoned.entity_id = target->id;
				event.poisoned.pos = (v2)target->pos;
				events.append(event);

				auto player = get_player(game);
				if (target->id == player->id) {
					auto poison_card = add_card(game, CARD_APPEARANCE_POISON);
					event = {};
					event.type = EVENT_ADD_CARD_TO_DISCARD;
					event.time = time;
					event.add_card_to_discard.entity_id = player->id;
					event.add_card_to_discard.card_id = poison_card->id;
					event.add_card_to_discard.appearance = poison_card->appearance;
					events.append(event);
				}

				break;
			}

			case TRANSACTION_BUMP_ATTACK_SNEAK_CONNECT: {
				t->type = TRANSACTION_REMOVE;

				Entity *attacker = game_get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = game_get_entity_by_id(game, t->bump_attack.target_id);
				if (!attacker || !target) {
					break;
				}

				Entity_Damage damage = {};
				damage.entity_id = t->bump_attack.target_id;
				damage.damage = 2;
				damage.pos = (v2)t->bump_attack.end;
				entity_damage.append(damage);

				Event event = {};
				event.type = EVENT_BUMP_ATTACK;
				// XXX - yuck
				event.time = time - constants.anims.bump_attack.duration / 2.0f;
				event.bump_attack.attacker_id = t->bump_attack.attacker_id;
				event.bump_attack.start = t->bump_attack.start;
				event.bump_attack.end = t->bump_attack.end;
				event.bump_attack.sound = SOUND_STAB_1_2;
				events.append(event);

				attacker->flags = (Entity_Flag)(attacker->flags & ~ENTITY_FLAG_INVISIBLE);

				event = {};
				event.type = EVENT_TURN_VISIBLE;
				event.turn_visible.entity_id = t->bump_attack.attacker_id;
				events.append(event);

				break;
			}


			case TRANSACTION_OPEN_DOOR: {
				t->type = TRANSACTION_REMOVE;

				Entity_ID door_id = t->open_door.door_id;
				Entity *door = game_get_entity_by_id(game, door_id);
				if (!door) {
					break;
				}

				Event event = {};
				event.type = EVENT_OPEN_DOOR;
				event.time = time;
				event.open_door.door_id = door_id;
				event.open_door.new_appearance = APPEARANCE_DOOR_WOODEN_OPEN;
				events.append(event);
				door->appearance = APPEARANCE_DOOR_WOODEN_OPEN;
				door->flags = (Entity_Flag)(door->flags | ENTITY_FLAG_BLOCKS_VISION);
				door->block_mask = 0;
				door->default_action = ACTION_NONE;

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

				Entity *a = game_get_entity_by_id(game, t->exchange.a);
				Entity *b = game_get_entity_by_id(game, t->exchange.b);

				Event event = {};
				event.type = EVENT_EXCHANGE;
				event.time = time;
				event.exchange.a = t->exchange.a;
				event.exchange.b = t->exchange.b;
				event.exchange.a_pos = a->pos;
				event.exchange.b_pos = b->pos;
				events.append(event);

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
				game_dispatch_message(game, m, time, transactions, events, NULL);

				m.type = MESSAGE_MOVE_POST_ENTER;
				game_dispatch_message(game, m, time, transactions, events, NULL);
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

				Event event = {};
				event.type = EVENT_CARD_POISON;
				event.time = time;
				event.card_poison.card_id = t->poison.card_id;
				events.append(event);

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
					Entity_ID new_id = add_slime(game,
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

			case TRANSACTION_SHOOT_WEB_CAST: {
				auto caster = game_get_entity_by_id(game, t->shoot_web.caster_id);
				if (!caster) {
					t->type = TRANSACTION_REMOVE;
					break;
				}
				t->type = TRANSACTION_SHOOT_WEB_HIT;
				t->start_time += constants.anims.shoot_web.cast_time;

				Event e = {};
				e.type = EVENT_SHOOT_WEB_CAST;
				e.time = t->start_time;
				e.shoot_web_cast.caster_id = t->shoot_web.caster_id;
				e.shoot_web_cast.start = (v2)caster->pos;
				e.shoot_web_cast.end = (v2)t->shoot_web.target;
				events.append(e);

				t->start_time += constants.anims.shoot_web.shot_duration;

				break;
			}
			case TRANSACTION_SHOOT_WEB_HIT: {
				t->type = TRANSACTION_REMOVE;

				auto web = add_spiderweb(game, t->shoot_web.target);

				Event e = {};
				e.type = EVENT_SHOOT_WEB_HIT;
				e.time = t->start_time;
				e.shoot_web_hit.pos = (v2)t->shoot_web.target;
				e.shoot_web_hit.web_id = web->id;
				e.shoot_web_hit.appearance = web->appearance;
				events.append(e);

				break;
			}

			case TRANSACTION_CREATURE_DROP_IN: {
				t->type = TRANSACTION_REMOVE;

				auto entity = add_creature(game, t->creature_drop_in.pos, t->creature_drop_in.type);

				Event e = {};
				e.type = EVENT_CREATURE_DROP_IN;
				e.time = t->start_time;
				e.creature_drop_in.entity_id = entity->id;
				e.creature_drop_in.pos = (v2)t->creature_drop_in.pos;
				e.creature_drop_in.appearance = get_creature_appearance(t->creature_drop_in.type);
				events.append(e);

				break;
			}

			case TRANSACTION_ADD_CREATURE: {
				auto entity = add_creature(game, t->add_creature.pos, t->add_creature.type);

				Event event = {};
				event.type = EVENT_ADD_CREATURE;
				event.time = t->start_time;
				event.add_creature.creature_id = entity->id;
				event.add_creature.appearance = entity->appearance;
				event.add_creature.pos = (v2)entity->pos;
				events.append(event);

				t->type = TRANSACTION_REMOVE;
				break;
			}

			case TRANSACTION_TURN_INVISIBLE_CAST: {
				t->type = TRANSACTION_TURN_INVISIBLE;
				t->start_time += constants.anims.turn_invisible.cast_time;
				break;
			}

			case TRANSACTION_TURN_INVISIBLE: {
				t->type = TRANSACTION_REMOVE;

				auto entity_id = t->turn_invisible.entity_id;
				auto entity = game_get_entity_by_id(game, entity_id);

				entity->flags = (Entity_Flag)(ENTITY_FLAG_INVISIBLE | entity->flags);

				Event e = {};
				e.type = EVENT_TURN_INVISIBLE;
				e.time = t->start_time;
				e.turn_invisible.entity_id = entity_id;
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
			game_dispatch_message(game, m, time, transactions, events, NULL);

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
				game_dispatch_message(game, m, time, transactions, events, NULL);

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
		update_fov(game);
		Event e = {};
		e.type = EVENT_FIELD_OF_VISION_CHANGED;
		e.time = 0.0f; // XXX
		e.field_of_vision.duration = constants.anims.move.duration;
		e.field_of_vision.fov = &game->field_of_vision;
		events.append(e);
	}
}

void game_do_turn(Game* game, Output_Buffer<Event> events)
{
	events.reset();

	// =====================================================================
	// create chosen actions

	// TODO -- safe function which checks for one action being
	// registered per entity

	Max_Length_Array<Action, MAX_ENTITIES> actions;
	actions.reset();

	// Entity_ID player_id = game_get_play
	Entity_ID player_id = game_get_player_id(game);
	u32 num_controllers = game->controllers.len;

	bool entity_has_acted[MAX_ENTITIES] = {};

	// 0. create bump attacks

	for (u32 i = 0; i < num_controllers; ++i) {
		Controller *c = &game->controllers[i];
		make_bump_attacks(c, game, actions);
	}

	for (u32 i = 0; i < actions.len; ++i) {
		auto action = &actions[i];
		if (action->entity_id) {
			entity_has_acted[action->entity_id] = true;
		}
	}

	// 1. choose moves

	Max_Length_Array<Potential_Move, MAX_ENTITIES * 9> potential_moves;
	potential_moves.reset();

	Map_Cache_Bool occupied;
	occupied.reset();

	auto& tiles = game->tiles;

	for (u32 i = 0; i < num_controllers; ++i) {
		Controller *c = &game->controllers[i];
		make_moves(c, game, potential_moves);
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

		if (!entity_has_acted[pm.entity_id]) {
			Action chosen_move = {};
			chosen_move.type = ACTION_MOVE;
			chosen_move.entity_id = pm.entity_id;
			chosen_move.move.start = pm.start;
			chosen_move.move.end = pm.end;
			actions.append(chosen_move);

			occupied.unset(pm.start);
			occupied.set(pm.end);

			entity_has_acted[pm.entity_id] = true;
		}

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
		make_actions(c, game, Slice<bool>(entity_has_acted, ARRAY_SIZE(entity_has_acted)), actions);
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

	game_simulate_actions(game, actions, events);
}

// =============================================================================
// card anim
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
	u32 num_card_anims = card_anim_state->card_anims.len;
	u32 selected_card_index = card_anim_state->hand_size;
	Card_Anim *anim = card_anim_state->card_anims.items;
	// log(debug_log, "highlighted_card_id = %u", highlighted_card_id);
	for (u32 i = 0; i < num_card_anims; ++i, ++anim) {
		if (anim->card_id == highlighted_card_id) {
			switch (anim->type) {
			case CARD_ANIM_IN_HAND:
				selected_card_index = anim->hand.index;
				// log(debug_log, "set selected_card_index 1");
				break;
			case CARD_ANIM_HAND_TO_HAND:
				// log(debug_log, "set selected_card_index 2");
				selected_card_index = anim->hand_to_hand.index;
				break;
			default:
				break;
			}
			break;
		}
	}
	// log(debug_log, "num_card_anims: %u", num_card_anims);
	if (selected_card_index < card_anim_state->hand_size) {
		// log(debug_log, "selected_card_index < card_anim_state->hand_size");
		hand_calc_deltas(deltas, &hand_params, selected_card_index);
	} else {
		// log(debug_log, "selected_card_index >= card_anim_state->hand_size");
		highlighted_card_id = 0;
		memset(deltas, 0, selected_card_index * sizeof(deltas[0]));
	}

	f32 base_delta = 0.0f;
	if (hand_params.num_cards > 1.0f) {
		base_delta = 1.0f / (hand_params.num_cards - 1.0f);
	}

	f32 ratio = hand_params.screen_width;

	anim = card_anim_state->card_anims.items;
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

		case CARD_ANIM_ADD_CARD_TO_DISCARD: {
			f32 t = time - card_anim_state->dyn_time_start - anim->add_to_discard.start_time;
			f32 dt = t / anim->add_to_discard.duration;
			dt = clamp(dt, 0.0f, 1.0f);

			v2 start_pos = v2(ratio - hand_params.border / 2.0f, -1.0f - hand_params.height);
			f32 start_angle = 0.0f;
			f32 start_zoom = 1.0f;

			v2 end_pos = v2(ratio - hand_params.border / 2.0f, -1.0f + hand_params.height / 2.0f);
			f32 end_angle = 0.0f;
			f32 end_zoom = 1.0f;

			anim->pos.type = CARD_POS_ABSOLUTE;
			anim->pos.absolute.pos   = lerp(start_pos,   end_pos,   dt);
			anim->pos.absolute.angle = lerp(start_angle, end_angle, dt);
			anim->pos.absolute.zoom  = lerp(start_zoom,  end_zoom,  dt);
			anim->pos.z_offset = 0.1f;

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

	u32 num_card_anims = card_anim_state->card_anims.len;
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
			card_anim_state->card_anims.append(anim);
			break;
		}
		case CARD_EVENT_HAND_TO_IN_PLAY: {
			do_hand_to_hand = 1;
			Card_Anim *anim = card_anim_state->card_anims.items;
			u32 num_card_anims = card_anim_state->card_anims.len;
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
			anim = card_anim_state->card_anims.items;
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
			Card_Anim *anim = card_anim_state->card_anims.items;
			u32 num_card_anims = card_anim_state->card_anims.len;
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
			anim = card_anim_state->card_anims.items;
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
			Card_Anim *anim = card_anim_state->card_anims.items;
			u32 num_card_anims = card_anim_state->card_anims.len;
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
	u32 num_card_anims = card_anim_state->card_anims.len;

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
			Card_Anim *anim = card_anim_state->card_anims.items;
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
	for (u32 i = 0; i < card_anim_state->card_anims.len; ) {
		Card_Anim *anim = &card_anim_state->card_anims[i];
		switch (anim->type) {
		case CARD_ANIM_DECK:
		case CARD_ANIM_DISCARD:
		case CARD_ANIM_IN_HAND:
		case CARD_ANIM_IN_PLAY:
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
				card_anim_state->card_anims.remove(i);
				continue;
			}
			break;
		case CARD_ANIM_HAND_TO_DISCARD:
			if (anim->hand_to_discard.start_time + anim->hand_to_discard.duration <= time) {
				card_anim_state->card_anims.remove(i);
				continue;
			}
			break;
		case CARD_ANIM_ADD_CARD_TO_DISCARD: {
			f32 t = time - anim->add_to_discard.start_time - card_anim_state->dyn_time_start;
			if (anim->add_to_discard.duration <= t) {
				card_anim_state->card_anims.remove(i);
				continue;
			}
			break;
		}
		default:
			ASSERT(0);
			break;
		}
		++i;
	}
	num_card_anims = card_anim_state->card_anims.len;

	auto &card_anim_mods = card_anim_state->card_anim_modifiers;
	f32 dyn_time = time - card_anim_state->dyn_time_start;
	for (u32 i = 0; i < card_anim_mods.len; ) {
		auto anim = &card_anim_mods[i];
		switch (anim->type) {
		case CARD_ANIM_MOD_FLASH:
			if (anim->flash.start_time + anim->flash.duration < dyn_time) {
				card_anim_mods.remove(i);
				continue;
			}
			break;
		default:
			ASSERT(0);
			break;
		}
		++i;
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
		Card_Anim *anim = card_anim_state->card_anims.items;
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

	Card_Anim *anim = card_anim_state->card_anims.items;
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
			break;
		}
		default:
			break;
		}

		for (u32 i = 0; i < card_anim_mods.len; ++i) {
			auto card_modifier = &card_anim_mods[i];
			if (card_modifier->card_id != anim->card_id) {
				continue;
			}

			switch (card_modifier->type) {
			case CARD_ANIM_MOD_FLASH: {
				f32 dt = (dyn_time - card_modifier->flash.start_time);
				u32 flash_counter = (u32)floorf(dt / card_modifier->flash.flash_duration);
				if (flash_counter % 2 == 0) {
					instance.color_mod = card_modifier->flash.color;
				}
				break;
			}
			}
		}

		card_render_add_instance(card_render, instance);
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
	Stack<Card_Param, MAX_CARD_PARAMS> card_params_stack;
	Action                             action_being_built;

	Anim_State                         anim_state;
	Render                            *render;
	Draw                              *draw;
	Sound_Player                       sound;

	u32           frame_number;
	v2            screen_size;
	v2_u32        max_screen_size;

	MT19937 random_state;

	u32 prev_card_id;

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
	memset(card_state, 0, sizeof(*card_state));
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

	program->draw->camera.zoom = 14.0f;
	Pos player_pos = game_get_player_pos(&program->game);
	program->draw->camera.world_center = (v2)player_pos;

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

	// program->draw->camera.zoom;

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
	program->draw->camera.world_center = (v2)top_left + (v2)size / 2.0f;
	program->draw->camera.zoom = (f32)size.h;

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
			f32 raw_zoom = (f32)screen_size.y / program->draw->camera.zoom;
			program->draw->camera.world_center -= (v2)input->mouse_delta / raw_zoom;
		} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
		             & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
			program->program_input_state_stack.pop();
		}
		break;
	}
	program->draw->camera.zoom -= (f32)input->mouse_wheel_delta * 0.25f;

	if (input_get_num_up_transitions(input, INPUT_BUTTON_F1) % 2) {
		program->display_debug_ui = !program->display_debug_ui;
	}
	if (input_get_num_up_transitions(input, INPUT_BUTTON_F2) % 2) {
		program->is_console_visible = !program->is_console_visible;
	}

	f32 time = (f32)program->frame_number / 60.0f;
	program->draw->renderer.time = time;
	draw(&program->anim_state, program->render, &program->sound, time);
	program->draw->camera.offset = program->anim_state.camera_offset;

	if (program->program_input_state_stack.peek() == GIS_ANIMATING && !is_animating(&program->anim_state)) {
		program->program_input_state_stack.pop();
	}

	v2_i32 world_mouse_pos = (v2_i32)screen_pos_to_world_pos(&program->draw->camera,
	                                                         screen_size,
	                                                         input->mouse_pos);
	u32 sprite_id = sprite_sheet_renderer_id_in_pos(&program->draw->renderer,
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

	Max_Length_Array<Event, MAX_EVENTS> event_buffer;
	event_buffer.reset();

	// program->card_anim_state.hand_params.screen_width = ratio;
	program->anim_state.card_anim_state.hand_params.height = constants.cards_ui.height;
	program->anim_state.card_anim_state.hand_params.border = constants.cards_ui.border;
	program->anim_state.card_anim_state.hand_params.top    = constants.cards_ui.top;
	program->anim_state.card_anim_state.hand_params.bottom = constants.cards_ui.bottom;
	// XXX - ugh
	program->anim_state.card_anim_state.hand_params.card_size = { 0.5f*0.4f*48.0f/80.0f, 0.5f*0.4f*1.0f };

	debug_line_reset(&program->draw->card_debug_line);
	// card_anim_update_anims
	u8 allow_highlight_card = program->program_input_state_stack.peek() == GIS_NONE;
	allow_highlight_card |= program->program_input_state_stack.peek() == GIS_PLAYING_CARDS;
	Card_UI_Event card_event = card_anim_draw(&program->anim_state.card_anim_state,
	                                          &program->game.card_state,
	                                          &program->draw->card_render,
	                                          &program->sound,
	                                          screen_size,
	                                          time,
	                                          input,
	                                          allow_highlight_card);

	// TODO -- get card id from mouse pos
	v2 card_mouse_pos = (v2)input->mouse_pos / (v2)screen_size;
	card_mouse_pos.x = (card_mouse_pos.x * 2.0f - 1.0f) * ((f32)screen_size.x / (f32)screen_size.y);
	card_mouse_pos.y = 1.0f - 2.0f * card_mouse_pos.y;
	u32 selected_card_id = card_render_get_card_id_from_mouse_pos(&program->draw->card_render,
	                                                              card_mouse_pos);
	program->prev_card_id = selected_card_id;
	{
		f32 ratio = (f32)screen_size.x / (f32) screen_size.y;
		program->draw->card_debug_line.constants.top_left     = { -ratio,  1.0f };
		program->draw->card_debug_line.constants.bottom_right = {  ratio, -1.0f };
	}
	/*
	card_render_draw_debug_lines(&program->draw->card_render,
	                             &program->draw->card_debug_line,
	                             selected_card_id);
	*/

	if (selected_card_id) {
		sprite_id = 0;
	}

	switch (program->program_input_state_stack.peek()) {
	case GIS_ANIMATING:
		sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, 0);
		break;
	case GIS_PLAYING_CARDS: {
		// Action player_action = {};
		// player_action.type = ACTION_NONE;
		// player_action.entity_id = game_get_player_id(&program->game);
		program->action_being_built.type = ACTION_NONE;
		program->action_being_built.entity_id = game_get_player_id(&program->game);

		Card_State *card_state = &program->game.card_state;
		Max_Length_Array<Card_Event, CARD_STATE_MAX_EVENTS> card_events;
		card_events.reset();

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
				card_events.append(card_event);

				Controller *c = &program->game.controllers[0];
				ASSERT(c->type == CONTROLLER_PLAYER);

				program->action_being_built.type = ACTION_FIRE_BOLT;

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
				card_events.append(card_event);

				card_state->in_play.append(*card);
				card_state->hand.remove_preserve_order(hand_index);
				break;
			}
			case CARD_APPEARANCE_FIREBALL: {
				Card_Event card_event = {};
				card_event.type = CARD_EVENT_SELECT;
				card_event.select.card_id = card_id;
				card_events.append(card_event);

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
				card_events.append(card_event);

				Entity *player = game_get_entity_by_id(&program->game, ENTITY_ID_PLAYER);
				ASSERT(player);

				program->action_being_built.type = ACTION_LIGHTNING;
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
				card_events.append(card_event);

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
				card_events.append(card_event);

				// card_state->in_play.append(*card);
				// card_state->hand.remove_preserve_order(hand_index);

				Controller *c = &program->game.controllers.items[0];
				ASSERT(c->type == CONTROLLER_PLAYER);
				Entity *player = game_get_entity_by_id(&program->game,
				                                       c->player.entity_id);
				program->action_being_built.type = ACTION_BLINK;

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
			game_end_play_cards(&program->game, card_events);
			Action player_action = {};
			player_action.type = ACTION_WAIT;
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			player_action.entity_id = c->player.entity_id;
			c->player.action = player_action;
			// Event_Buffer event_buffer;
			event_buffer.reset();
			game_do_turn(&program->game, event_buffer);
			build_animations(&program->anim_state, event_buffer, time);
			program->program_input_state_stack.pop();
			program->program_input_state_stack.push(GIS_ANIMATING);
		}

		card_anim_update_anims(&program->anim_state.card_anim_state, card_events, time);

		sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, sprite_id);

		break;
	}
	case GIS_NONE: {
		Action player_action = {};
		player_action.type = ACTION_NONE;
		player_action.entity_id = game_get_player_id(&program->game);

		Card_State *card_state = &program->game.card_state;
		Max_Length_Array<Card_Event, CARD_STATE_MAX_EVENTS> card_events;
		card_events.reset();

		switch (card_event.type) {
		case CARD_UI_EVENT_NONE:
			break;
		case CARD_UI_EVENT_DECK_CLICKED: {
			Action_Buffer actions;
			game_draw_cards(&program->game, &actions, 5, card_events);
			if (actions) {
				// Event_Buffer events;
				event_buffer.reset();
				game_simulate_actions(&program->game, actions, event_buffer);
				build_animations(&program->anim_state, event_buffer, time);
				program->program_input_state_stack.push(GIS_ANIMATING);
			}
			program->program_input_state_stack.push(GIS_PLAYING_CARDS);
			break;
		}
		case CARD_UI_EVENT_DISCARD_CLICKED: {
			Action_Buffer actions;
			game_draw_cards(&program->game, &actions, 5, card_events);
			if (actions) {
				// Event_Buffer events;
				event_buffer.reset();
				game_simulate_actions(&program->game, actions, event_buffer);
				build_animations(&program->anim_state, event_buffer, time);
				program->program_input_state_stack.push(GIS_ANIMATING);
			}
			program->program_input_state_stack.push(GIS_PLAYING_CARDS);
			break;
		}
		}

		card_anim_update_anims(&program->anim_state.card_anim_state, card_events, time);

		if (sprite_id && input->num_presses(INPUT_BUTTON_MOUSE_LEFT)) {
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			Entity *player = game_get_entity_by_id(&program->game, c->player.entity_id);
			Action_Type action = ACTION_MOVE;
			Pos start = player->pos;
			Entity *e = NULL;
			Pos end;
			if (sprite_id < MAX_ENTITIES) {
				e = game_get_entity_by_id(&program->game, sprite_id);
				ASSERT(e);
				end = e->pos;
				if (e->default_action) {
					action = e->default_action;
				}
			} else {
				end = u16_to_pos(sprite_id - MAX_ENTITIES);
			}

			switch (action) {
			case ACTION_MOVE: {
				v2 dir = (v2)end - (v2)start;
				dir = dir * dir;
				bool test = (dir.x || dir.y) && (dir.x <= 1 && dir.y <= 1);
				if (game_is_passable(&program->game, end, player->movement_type) && test) {
					player_action.type = ACTION_MOVE;
					player_action.entity_id = player->id;
					player_action.move.start = start;
					player_action.move.end = end;
				}
				break;
			}
			case ACTION_OPEN_DOOR: {
				v2 dir = (v2)end - (v2)start;
				dir = dir * dir;
				bool test = (dir.x || dir.y) && (dir.x <= 1 && dir.y <= 1);

				if (test) {
					player_action.type = ACTION_OPEN_DOOR;
					player_action.entity_id = player->id;
					player_action.open_door.door_id = e->id;
				}
				
				break;
			}
			case ACTION_BUMP_ATTACK: {
				v2 dir = (v2)end - (v2)start;
				dir = dir * dir;
				bool test = (dir.x || dir.y) && (dir.x <= 1 && dir.y <= 1);

				if (test) {
					player_action.type = ACTION_BUMP_ATTACK;
					player_action.entity_id = player->id;
					player_action.bump_attack.target_id = e->id;
				}
				break;
			}

			}
		}

		if (player_action) {
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			c->player.action = player_action;
			event_buffer.reset();
			game_do_turn(&program->game, event_buffer);
			build_animations(&program->anim_state, event_buffer, time);
			program->program_input_state_stack.push(GIS_ANIMATING);
		}

		sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, sprite_id);
		break;
	}
	case GIS_CARD_PARAMS: {
		Card_Param *param = program->card_params_stack.peek_ptr();
		Max_Length_Array<Card_Event, CARD_STATE_MAX_EVENTS> card_events;
		card_events.reset();

		switch (param->type) {
		case CARD_PARAM_TARGET:
			sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, sprite_id);
			if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
				if (!sprite_id) {
					break;
				}
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
				sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, 0);
				break;
			}
			sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, sprite_id);
			if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
				*param->creature.id = e->id;
				program->card_params_stack.pop();
			}
			break;
		}
		case CARD_PARAM_AVAILABLE_TILE: {
			sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, 0);
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
			sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, sprite_id);
			if (input_get_num_up_transitions(input, INPUT_BUTTON_MOUSE_LEFT)) {
				*param->available_tile.dest = target;
				program->card_params_stack.pop();
			}
			break;
		}
		}

		if (!program->card_params_stack) {
			Card_Anim_State *card_anim_state = &program->anim_state.card_anim_state;
			ASSERT(card_anim_state->type == CARD_ANIM_STATE_SELECTED
			    || card_anim_state->type == CARD_ANIM_STATE_NORMAL_TO_SELECTED);

			Card_State *card_state = &program->game.card_state;
			Card_Event event = {};
			event.type = CARD_EVENT_UNSELECT;
			card_events.append(event);

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
			card_events.append(event);

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
			card_anim_update_anims(card_anim_state, card_events, time);

			// TODO -- cast spell
			Controller *c = &program->game.controllers.items[0];
			ASSERT(c->type == CONTROLLER_PLAYER);
			c->player.action = program->action_being_built;
			event_buffer.reset();
			game_simulate_actions(&program->game,
			                      slice_one(&program->action_being_built),
			                      event_buffer);
			build_animations(&program->anim_state, event_buffer, time);
			program->program_input_state_stack.pop();
			program->program_input_state_stack.push(GIS_ANIMATING);
		}
		break;
	}
	case GIS_DRAGGING_MAP:
		sprite_sheet_renderer_highlight_sprite(&program->draw->renderer, 0);
		break;
	}

	// imgui
	IMGUI_Context *ic = &program->draw->imgui;
	imgui_begin(ic, input);
	if (program->display_debug_ui) {
		imgui_set_text_cursor(ic, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
		if (imgui_tree_begin(ic, "show mouse info")) {
			imgui_text(ic, "Mouse Pos: (%u, %u)", input->mouse_pos.x, input->mouse_pos.y);
			imgui_text(ic, "Mouse Delta: (%d, %d)",
			           input->mouse_delta.x, input->mouse_delta.y);
			imgui_text(ic, "Mouse world pos: (%d, %d)", world_mouse_pos.x, world_mouse_pos.y);
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
			imgui_text(ic, "Sprite ID: %u, position: (%u, %u)", sprite_id,
			           sprite_pos.x, sprite_pos.y);
			imgui_text(ic, "Card mouse pos: (%f, %f)", card_mouse_pos.x, card_mouse_pos.y);
			imgui_text(ic, "Selected Card ID: %u", selected_card_id);
			imgui_text(ic, "World Center: (%f, %f)", program->draw->camera.world_center.x,
			           program->draw->camera.world_center.y);
			imgui_text(ic, "Zoom: %f", program->draw->camera.zoom);
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
	if (program->display_fog_of_war) {
		render(&program->game.field_of_vision, program->render);
	} else {
		Field_Of_Vision fov = {};
		for (u32 y = 1; y < 255; ++y) {
			for (u32 x = 1; x < 255; ++x) {
				fov[Pos(x, y)] = FOV_VISIBLE;
			}
		}
		render(&fov, program->render);
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

		v2 world_tl = screen_pos_to_world_pos(&program->draw->camera,
						screen_size,
						v2_u32(0, 0));
		v2 world_br = screen_pos_to_world_pos(&program->draw->camera,
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
		v2 world_tl = screen_pos_to_world_pos(&program->draw->camera, screen_size, { 0, 0 });
		v2 world_br = screen_pos_to_world_pos(&program->draw->camera, screen_size, screen_size);
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
				f32 raw_zoom = (f32)screen_size.y / program->draw->camera.zoom;
				program->draw->camera.world_center -= (v2)input->mouse_delta / raw_zoom;
			} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
				     & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
				program->program_input_state_stack.pop();
			}
			break;
		}
		program->draw->camera.zoom -= (f32)input->mouse_wheel_delta * 0.25f;
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