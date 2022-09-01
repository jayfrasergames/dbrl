#include "game.h"

#include "stdafx.h"
#include "random.h"
#include "physics.h"
#include "constants.h"

static Entity_ID new_entity_id(Game* game)
{
	return game->next_entity_id++;
}

static void remove_entity(Game* game, Entity_ID entity_id)
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

Entity* add_entity(Game* game)
{
	auto entity = game->entities.append();
	memset(entity, 0, sizeof(*entity));
	entity->id = new_entity_id(game);
	return entity;
}

Controller* add_controller(Game* game)
{
	auto controller = game->controllers.append();
	controller->id = game->next_controller_id++;
	return controller;
}

Message_Handler* add_message_handler(Game* game)
{
	auto handler = game->handlers.append();
	return handler;
}

Card* add_card(Game* game, Card_Appearance appearance)
{
	// XXX -- for now add the card to the discard pile
	auto card = game->card_state.discard.append();
	card->id = game->next_card_id++;
	card->appearance = appearance;
	return card;
}

void init(Game* game)
{
	memset(game, 0, sizeof(*game));
	game->next_entity_id = 1;
	game->next_controller_id = 1;
	game->next_card_id = 1;

	auto player = add_entity(game);
	ASSERT(player->id == ENTITY_ID_PLAYER);
	game->player_id = ENTITY_ID_PLAYER;
	game->next_entity_id = NUM_STATIC_ENTITY_IDS;

	player->hit_points = 100;
	player->max_hit_points = 100;
	player->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;
	player->appearance = APPEARANCE_CREATURE_MALE_BERSERKER;
	player->movement_type = BLOCK_WALK;

	auto controller = add_controller(game);
	controller->type = CONTROLLER_PLAYER;
	controller->player.entity_id = player->id;
	controller->player.action.type = ACTION_NONE;

	game->card_state.hand_size = constants.rules.initial_hand_size;
}

Entity* get_entity_by_id(Game* game, Entity_ID entity_id)
{
	for (u32 i = 0; i < game->entities.len; ++i) {
		if (game->entities[i].id == entity_id) {
			return &game->entities[i];
		}
	}
	return NULL;
}

Entity* get_player(Game* game)
{
	return get_entity_by_id(game, game->player_id);
}

bool game_is_pos_opaque(Game* game, Pos pos)
{
	// TODO -- check entities blocking pos?
	auto& tiles = game->tiles;
	switch (tiles[pos].type) {
	case TILE_EMPTY:
	case TILE_FLOOR:
	case TILE_WATER:
		return false;
		break;
	case TILE_WALL:
		return true;
		break;
	default:
		ASSERT(0);
	}
	return false;
}

bool tile_is_passable(Tile tile, u16 move_mask)
{
	switch (tile.type) {
	case TILE_EMPTY:
	case TILE_WALL:
		return false;
	case TILE_FLOOR:
		if (move_mask & BLOCK_SWIM) {
			return false;
		}
		return true;
	case TILE_WATER:
		if (move_mask & BLOCK_WALK) {
			return false;
		}
		return true;
	}
	ASSERT(0);
	return false;
}

bool is_pos_passable(Game* game, Pos pos, u16 move_mask)
{
	auto tile = game->tiles[pos];
	if (!tile_is_passable(tile, move_mask)) {
		return false;
	}

	auto &entities = game->entities;
	for (u32 i = 0; i < entities.len; ++i) {
		auto entity = &entities[i];
		if (entity->pos == pos && entity->block_mask & move_mask) {
			return false;
		}
	}

	return true;
}

void update_fov(Game* game)
{
	Map_Cache_Bool map = {}, fov = {};
	map.reset();

	auto& tiles = game->tiles;
	for (u16 y = 0; y < 256; ++y) {
		for (u16 x = 0; x < 256; ++x) {
			Pos p = Pos((u8)x, (u8)y);
			if (game_is_pos_opaque(game, p)) {
				map.set(p);
			}
		}
	}

	auto& entities = game->entities;
	for (u32 i = 0; i < entities.len; ++i) {
		if (entities[i].flags & ENTITY_FLAG_BLOCKS_VISION) {
			map.set(entities[i].pos);
		}
	}

	auto player = get_entity_by_id(game, game->player_id);
	ASSERT(player);

	calculate_fov(&fov, &map, player->pos);
	update(&game->field_of_vision, &fov);
}

// ============================================================================
// creatures
// ============================================================================

Entity* add_enemy(Game* game, u32 hit_points)
{
	auto e = add_entity(game);
	e->hit_points = hit_points;
	e->max_hit_points = hit_points;
	e->default_action = ACTION_BUMP_ATTACK;
	e->block_mask = BLOCK_WALK | BLOCK_SWIM | BLOCK_FLY;

	return e;
}

Entity* add_creature(Game* game, Pos pos, Creature_Type type)
{
	switch (type) {
	case CREATURE_SPIDER_NORMAL:
		return add_spider_normal(game, pos);
	case CREATURE_SPIDER_WEB:
		return add_spider_web(game, pos);
	case CREATURE_SPIDER_POISON:
		return add_spider_poison(game, pos);
	case CREATURE_SPIDER_SHADOW:
		return add_spider_shadow(game, pos);
	case CREATURE_IMP:
		return add_imp(game, pos);
	default:
		ASSERT(0);
	}
	return NULL;
}

Appearance get_creature_appearance(Creature_Type type)
{
	switch (type) {
	case CREATURE_SPIDER_NORMAL: return APPEARANCE_CREATURE_RED_SPIDER;
	case CREATURE_SPIDER_WEB:    return APPEARANCE_CREATURE_BLACK_SPIDER;
	case CREATURE_SPIDER_POISON: return APPEARANCE_CREATURE_SPIDER_GREEN;
	case CREATURE_SPIDER_SHADOW: return APPEARANCE_CREATURE_SPIDER_BLUE;
	}
	ASSERT(0);
	return APPEARANCE_NONE;
}

Entity* add_spider_normal(Game* game, Pos pos)
{
	auto e = add_enemy(game, 5);
	e->appearance = APPEARANCE_CREATURE_RED_SPIDER;
	e->movement_type = BLOCK_WALK;
	e->pos = pos;
	e->flags = ENTITY_FLAG_WALK_THROUGH_WEBS;

	auto c = add_controller(game);
	c->type = CONTROLLER_SPIDER_NORMAL;
	c->spider_normal.entity_id = e->id;

	return e;
}

Entity* add_spider_web(Game* game, Pos pos)
{
	auto e = add_enemy(game, 5);
	e->appearance = APPEARANCE_CREATURE_BLACK_SPIDER;
	e->movement_type = BLOCK_WALK;
	e->pos = pos;
	e->flags = ENTITY_FLAG_WALK_THROUGH_WEBS;

	auto c = add_controller(game);
	c->type = CONTROLLER_SPIDER_WEB;
	c->spider_web.entity_id = e->id;
	c->spider_web.web_cooldown = 3;

	return e;
}

Entity* add_spider_poison(Game* game, Pos pos)
{
	auto e = add_enemy(game, 5);
	e->appearance = APPEARANCE_CREATURE_SPIDER_GREEN;
	e->movement_type = BLOCK_WALK;
	e->pos = pos;
	e->flags = ENTITY_FLAG_WALK_THROUGH_WEBS;

	auto c = add_controller(game);
	c->type = CONTROLLER_SPIDER_POISON;
	c->spider_poison.entity_id = e->id;

	return e;
}

Entity* add_spider_shadow(Game* game, Pos pos)
{
	auto e = add_enemy(game, 5);
	e->appearance = APPEARANCE_CREATURE_SPIDER_BLUE;
	e->movement_type = BLOCK_WALK;
	e->pos = pos;
	e->flags = ENTITY_FLAG_WALK_THROUGH_WEBS;

	auto c = add_controller(game);
	c->type = CONTROLLER_SPIDER_SHADOW;
	c->spider_shadow.entity_id = e->id;
	c->spider_shadow.invisible_cooldown = 3;

	return e;
}

Entity* add_imp(Game* game, Pos pos)
{
	auto e = add_enemy(game, 5);
	e->appearance = APPEARANCE_CREATURE_IMP;
	e->movement_type = BLOCK_FLY;
	e->pos = pos;

	auto c = add_controller(game);
	c->type = CONTROLLER_IMP;
	c->imp.imp_id = e->id;

	return e;
}

Entity* add_spiderweb(Game* game, Pos pos)
{
	auto &tiles = game->tiles;
	bool top    = tiles[Pos(pos.x, pos.y - 1)].type == TILE_WALL;
	bool bottom = tiles[Pos(pos.x, pos.y + 1)].type == TILE_WALL;
	bool left   = tiles[Pos(pos.x - 1, pos.y)].type == TILE_WALL;
	bool right  = tiles[Pos(pos.x + 1, pos.y)].type == TILE_WALL;

	Appearance appearance;
	if        (top  &&  left && !bottom && !right) {
		appearance = APPEARANCE_ITEM_SPIDERWEB_TOP_LEFT;
	} else if (top  && !left && !bottom &&  right) {
		appearance = APPEARANCE_ITEM_SPIDERWEB_TOP_RIGHT;
	} else if (!top &&  left &&  bottom && !right) {
		appearance = APPEARANCE_ITEM_SPIDERWEB_BOTTOM_LEFT;
	} else if (!top && !left &&  bottom &&  right) {
		appearance = APPEARANCE_ITEM_SPIDERWEB_BOTTOM_RIGHT;
	} else {
		appearance = rand_u32() % 2 ? APPEARANCE_ITEM_SPIDERWEB_1 : APPEARANCE_ITEM_SPIDERWEB_2;
	}

	auto e = add_entity(game);
	e->hit_points = 1;
	e->max_hit_points = 1;
	e->appearance = appearance;
	e->pos = pos;

	auto mh = add_message_handler(game);
	mh->type = MESSAGE_HANDLER_SPIDER_WEB_PREVENT_EXIT;
	mh->handle_mask = MESSAGE_MOVE_PRE_EXIT;
	mh->owner_id = e->id;
	mh->prevent_exit.pos = pos;

	return e;
}

Message_Handler* add_trap_spider_cave(Game* game, Pos pos, u32 radius)
{
	auto mh = add_message_handler(game);
	mh->type = MESSAGE_HANDLER_TRAP_SPIDER_CAVE;
	mh->handle_mask = MESSAGE_MOVE_POST_ENTER;
	mh->trap_spider_cave.center = pos;
	mh->trap_spider_cave.radius = radius;
	mh->trap_spider_cave.last_dist_squared = radius*radius + 1;

	return mh;
}

// Old style

Entity_ID add_slime(Game *game, Pos pos, u32 hit_points)
{
	auto e = add_enemy(game, 5);
	e->hit_points = min_u32(hit_points, 5);
	e->pos = pos;
	e->appearance = APPEARANCE_CREATURE_GREEN_SLIME;
	e->movement_type = BLOCK_WALK;

	Controller c = {};
	c.type = CONTROLLER_SLIME;
	c.slime.entity_id = e->id;
	c.slime.split_cooldown = 5;
	game->controllers.append(c);

	Message_Handler mh = {};
	mh.type = MESSAGE_HANDLER_SLIME_SPLIT;
	mh.handle_mask = MESSAGE_DAMAGE;
	mh.owner_id = e->id;
	game->handlers.append(mh);

	return e->id;
}

// =============================================================================
// Controllers
// =============================================================================

static Entity_ID lich_get_skeleton_to_heal(Game *game, Controller *controller)
{
	Entity_ID best_skeleton_id = 0;
	if (!controller->lich.heal_cooldown) {
		auto& skeleton_ids = controller->lich.skeleton_ids;
		u32 num_skeleton_ids = skeleton_ids.len;
		i32 best_heal = 0;
		for (u32 i = 0; i < num_skeleton_ids; ++i) {
			Entity_ID e_id = skeleton_ids[i];
			Entity *e = get_entity_by_id(game, e_id);
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

static void do_action_if_adjacent(Game* game, Entity_ID actor_id, Entity_ID target_id, Output_Buffer<Action> actions, Action action)
{
	auto actor = get_entity_by_id(game, actor_id);
	auto target = get_entity_by_id(game, target_id);
	ASSERT(actor && target);

	if (positions_are_adjacent(actor->pos, target->pos)) {
		actions.append(action);
	}
}

static void bump_attack_player_if_adjacent(Game* game, Entity_ID entity_id, Output_Buffer<Action> attacks, Action_Type bump_attack_type = ACTION_BUMP_ATTACK)
{
	auto player = get_player(game);

	Entity *entity = get_entity_by_id(game, entity_id);
	if (positions_are_adjacent(player->pos, entity->pos)) {
		Action bump = {};
		bump.type = bump_attack_type;
		bump.entity_id = entity_id;
		bump.bump_attack.target_id = player->id;
		attacks.append(bump);
	}
}

void make_bump_attacks(Controller* c, Game* game, Output_Buffer<Action> attacks)
{
	switch (c->type) {
	case CONTROLLER_PLAYER:
		if (c->player.action.type == ACTION_BUMP_ATTACK) {
			attacks.append(c->player.action);
		}
		break;
	case CONTROLLER_SLIME: {
		bump_attack_player_if_adjacent(game, c->slime.entity_id, attacks);
		break;
	}
	case CONTROLLER_LICH: {
		u32 num_skeletons = c->lich.skeleton_ids.len;
		for (u32 i = 0; i < num_skeletons; ++i) {
			bump_attack_player_if_adjacent(game, c->lich.skeleton_ids[i], attacks);
		}
		break;
	}
	case CONTROLLER_SPIDER_NORMAL: {
		bump_attack_player_if_adjacent(game, c->spider_normal.entity_id, attacks);
		break;
	}
	case CONTROLLER_SPIDER_WEB: {
		bump_attack_player_if_adjacent(game, c->spider_web.entity_id, attacks);
		break;
	}
	case CONTROLLER_SPIDER_POISON: {
		bump_attack_player_if_adjacent(game, c->spider_poison.entity_id, attacks, ACTION_BUMP_ATTACK_POISON);
		break;
	}
	case CONTROLLER_SPIDER_SHADOW: {
		auto spider_id = c->spider_shadow.entity_id;
		auto spider = get_entity_by_id(game, spider_id);
		if (spider->flags & ENTITY_FLAG_INVISIBLE) {
			bump_attack_player_if_adjacent(game, spider_id, attacks, ACTION_BUMP_ATTACK_SNEAK);
		} else {
			bump_attack_player_if_adjacent(game, spider_id, attacks);
		}
		break;
	}
	case CONTROLLER_IMP: {
		Action steal_card = {};
		steal_card.type = ACTION_STEAL_CARD;
		steal_card.entity_id = c->imp.imp_id;
		steal_card.steal_card.target_id = ENTITY_ID_PLAYER;
		do_action_if_adjacent(game, steal_card.entity_id, ENTITY_ID_PLAYER, attacks, steal_card);
		break;
	}

	case CONTROLLER_RANDOM_MOVE:
	case CONTROLLER_DRAGON:
		break;
	}
}

static void move_toward_player(Game* game, Entity_ID entity_id, Output_Buffer<Potential_Move> potential_moves)
{
	auto entity = get_entity_by_id(game, entity_id);
	auto player = get_player(game);

	v2_i16 spider_pos = (v2_i16)entity->pos;
	v2_i16 player_pos = (v2_i16)player->pos;
	v2_i16 d = player_pos - spider_pos;
	u32 cur_dist_squared = d.x*d.x + d.y*d.y;

	auto &tiles = game->tiles;
	for (i16 dy = -1; dy <= 1; ++dy) {
		for (i16 dx = -1; dx <= 1; ++dx) {
			if (!dx && !dy) {
				continue;
			}
			v2_i16 end = spider_pos + v2_i16(dx, dy);
			d = player_pos - end;
			Tile t = tiles[(Pos)end];
			if (d.x*d.x + d.y*d.y <= cur_dist_squared
				&& tile_is_passable(t, entity->movement_type)) {
				Potential_Move pm = {};
				pm.entity_id = entity_id;
				pm.start = (Pos)spider_pos;
				pm.end = (Pos)end;
				pm.weight = uniform_f32(1.9f, 2.1f);
				potential_moves.append(pm);
			}
		}
	}
}

void make_moves(Controller* c, Game* game, Output_Buffer<Potential_Move> potential_moves)
{
	auto& tiles = game->tiles;

	switch (c->type) {
	case CONTROLLER_PLAYER:
		if (c->player.action.type == ACTION_MOVE) {
			Potential_Move pm = {};
			pm.entity_id = c->player.entity_id;
			pm.start = c->player.action.move.start;
			pm.end = c->player.action.move.end;
			// XXX -- weight here should be infinite
			pm.weight = 10.0f;
			potential_moves.append(pm);
		}
		break;
	case CONTROLLER_RANDOM_MOVE: {
		Entity *e = get_entity_by_id(game, c->random_move.entity_id);
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
		Entity *e = get_entity_by_id(game, c->slime.entity_id);
		// Pos p = e->pos;
		v2_i16 start = (v2_i16)e->pos;
		v2_i16 iplayer_pos = (v2_i16)get_player(game)->pos;
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
			Entity *e = get_entity_by_id(game, c->lich.lich_id);
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
			Entity *e = get_entity_by_id(game, skeleton_ids[i]);
			// Pos p = e->pos;
			v2_i16 start = (v2_i16)e->pos;
			v2_i16 iplayer_pos = (v2_i16)get_player(game)->pos;
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

	case CONTROLLER_SPIDER_NORMAL: {
		auto spider_id = c->spider_normal.entity_id;
		move_toward_player(game, spider_id, potential_moves);
		break;
	}

	case CONTROLLER_SPIDER_POISON: {
		auto spider_id = c->spider_poison.entity_id;
		move_toward_player(game, spider_id, potential_moves);
		break;
	}

	case CONTROLLER_SPIDER_WEB: {
		auto spider_id = c->spider_web.entity_id;
		if (c->spider_web.web_cooldown) {
			move_toward_player(game, spider_id, potential_moves);
		}
		break;
	}

	case CONTROLLER_SPIDER_SHADOW: {
		auto spider_id = c->spider_shadow.entity_id;
		auto spider = get_entity_by_id(game, spider_id);
		if (c->spider_shadow.invisible_cooldown || (spider->flags & ENTITY_FLAG_INVISIBLE)) {
			move_toward_player(game, spider_id, potential_moves);
		}
		break;
	}

	}
}

void make_actions(Controller* c, Game* game, Slice<bool> has_acted, Output_Buffer<Action> actions)
{
	switch (c->type) {
	case CONTROLLER_PLAYER:
		// if (c->player.action.type != ACTION_MOVE) {
		if (!has_acted[c->player.entity_id]) {
			actions.append(c->player.action);
		}
		break;
	case CONTROLLER_DRAGON: {
		Entity *e = get_entity_by_id(game, c->dragon.entity_id);
		u16 move_mask = e->movement_type;
		u32 t_idx = rand_u32() % game->entities.len;
		Entity *t = &game->entities[t_idx];
		Action a = {};
		a.type = ACTION_FIREBALL;
		a.entity_id = c->dragon.entity_id;
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
			a.entity_id = c->lich.lich_id;
			a.heal.target_id = skeleton_to_heal;
			a.heal.amount = 5;
			actions.append(a);
		} else if (c->lich.heal_cooldown) {
			--c->lich.heal_cooldown;
		}
		break;
	}

	case CONTROLLER_SPIDER_WEB: {
		auto spider_id = c->spider_web.entity_id;
		if (c->spider_web.web_cooldown) {
			--c->spider_web.web_cooldown;
		}
		if (has_acted[spider_id]) {
			return;
		}
		if (c->spider_web.web_cooldown) {
			return;
		}

		auto player = get_player(game);
		auto spider = get_entity_by_id(game, spider_id);

		const i16 radius = 3;

		Max_Length_Array<Pos, (2*radius + 1)*(2*radius + 1)> potential_targets = {};

		v2_i16 d = (v2_i16)player->pos - (v2_i16)spider->pos;
		if (d.x*d.x + d.y*d.y > radius*radius) {
			return;
		}

		auto &tiles = game->tiles;

		for (i16 dy = -radius; dy <= radius; ++dy) {
			for (i16 dx = -radius; dx <= radius; ++dx) {
				Pos p = (Pos)((v2_i16)spider->pos + v2_i16(dx, dy));
				if (tile_is_passable(tiles[p], BLOCK_WALK)) {
					// TODO -- check there is no web in 
					potential_targets.append(p);
				}
			}
		}

		if (potential_targets) {
			auto target = potential_targets[rand_u32() % potential_targets.len];

			Action action = {};
			action.type = ACTION_SHOOT_WEB;
			action.entity_id = spider_id;
			action.shoot_web.target = target;
			actions.append(action);

			c->spider_web.web_cooldown = 3;
		}
		break;
	}

	case CONTROLLER_SPIDER_SHADOW: {
		auto spider_id = c->spider_shadow.entity_id;
		auto spider = get_entity_by_id(game, spider_id);

		if (c->spider_shadow.invisible_cooldown) {
			--c->spider_shadow.invisible_cooldown;
		}
		if (has_acted[spider_id]) {
			return;
		}
		if (c->spider_shadow.invisible_cooldown || (spider->flags & ENTITY_FLAG_INVISIBLE)) {
			return;
		}

		Action action = {};
		action.type = ACTION_TURN_INVISIBLE;
		action.entity_id = spider_id;
		actions.append(action);

		c->spider_shadow.invisible_cooldown = 3;

		break;
	}

	}
}

// ============================================================================
// simulate
// ============================================================================

#define GAME_MAX_TRANSACTIONS 65536

enum Phase
{
	PHASE_CURRENT,

	PHASE_CARDS,
	PHASE_BUMP_ATTACK,
	PHASE_MOVE,
	PHASE_ACTION,

	NUM_PHASES,
};

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
	TRANSACTION_CLOSE_DOOR,
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
	TRANSACTION_STEAL_CARD,

	TRANSACTION_DRAW_CARDS_1,
	TRANSACTION_DRAW_CARDS_2,
	TRANSACTION_DISCARD_HAND,
	TRANSACTION_PLAY_CARD,
};

#define TRANSACTION_EPSILON 1e-6f;

struct Transaction
{
	Transaction_Type type;
	Phase            phase;
	f32              start_time;
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
			Entity_ID caster_id;
			Pos       end;
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
			Entity_ID entity_id;
			Entity_ID door_id;
		} close_door;
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
		struct {
			u32 num_cards_to_draw;
		} draw_cards;
		struct {
			Card_ID card_id;
			Action  action;
		} play_card;
		struct {
			Entity_ID stealer_id;
			Entity_ID target_id;
		} steal_card;
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
				auto entity = get_entity_by_id(game, message.move.entity_id);
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
				Entity *e = get_entity_by_id(game, h->owner_id);
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

				Entity *entity = get_entity_by_id(game, new_lich_id);
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

Transaction to_transaction(Action action)
{
	Transaction t = {};

	switch (action.type) {
	case ACTION_NONE:
	case ACTION_WAIT:
		break;
	case ACTION_MOVE: {
		t.type = TRANSACTION_MOVE_EXIT;
		t.phase = PHASE_MOVE;
		t.move.entity_id = action.entity_id;
		t.move.start = action.move.start;
		t.move.end = action.move.end;
		break;
	}
	case ACTION_BUMP_ATTACK: {
		t.type = TRANSACTION_BUMP_ATTACK;
		t.phase = PHASE_BUMP_ATTACK;
		t.bump_attack.attacker_id = action.entity_id;
		t.bump_attack.target_id = action.bump_attack.target_id;
		break;
	}
	case ACTION_BUMP_ATTACK_POISON: {
		t.type = TRANSACTION_BUMP_ATTACK_POISON;
		t.phase = PHASE_BUMP_ATTACK;
		t.bump_attack.attacker_id = action.entity_id;
		t.bump_attack.target_id = action.bump_attack.target_id;
		break;
	}
	case ACTION_STEAL_CARD: {
		t.type = TRANSACTION_STEAL_CARD;
		t.start_time = constants.anims.bump_attack.duration / 2.0f;
		t.phase = PHASE_BUMP_ATTACK;
		t.steal_card.stealer_id = action.entity_id;
		t.steal_card.target_id = action.steal_card.target_id;
		break;
	}
	case ACTION_BUMP_ATTACK_SNEAK: {
		t.type = TRANSACTION_BUMP_ATTACK_SNEAK;
		t.phase = PHASE_BUMP_ATTACK;
		t.bump_attack.attacker_id = action.entity_id;
		t.bump_attack.target_id = action.bump_attack.target_id;
		break;
	}
	case ACTION_OPEN_DOOR: {
		t.type = TRANSACTION_OPEN_DOOR;
		t.phase = PHASE_ACTION;
		t.open_door.entity_id = action.entity_id;
		t.open_door.door_id = action.open_door.door_id;
		break;
	}
	case ACTION_CLOSE_DOOR: {
		t.type = TRANSACTION_CLOSE_DOOR;
		t.phase = PHASE_ACTION;
		t.close_door.entity_id = action.entity_id;
		t.close_door.door_id = action.close_door.door_id;
		break;
	}
	case ACTION_FIREBALL: {
		t.type = TRANSACTION_FIREBALL_SHOT;
		t.phase = PHASE_ACTION;
		t.fireball_shot.caster_id = action.entity_id;
		t.fireball_shot.end = action.fireball.end;
		break;
	}
	case ACTION_EXCHANGE: {
		t.type = TRANSACTION_EXCHANGE_CAST;
		t.phase = PHASE_ACTION;
		t.exchange.a = action.exchange.a;
		t.exchange.b = action.exchange.b;
		break;
	}
	case ACTION_BLINK: {
		t.type = TRANSACTION_BLINK_CAST;
		t.phase = PHASE_ACTION;
		t.blink.caster_id = action.entity_id;
		t.blink.target = action.blink.target;
		break;
	}
	case ACTION_POISON: {
		ASSERT(0);
		break;
	}
	case ACTION_FIRE_BOLT: {
		t.type = TRANSACTION_FIRE_BOLT_CAST;
		t.phase = PHASE_ACTION;
		t.fire_bolt.caster_id = action.entity_id;
		t.fire_bolt.target_id = action.fire_bolt.target_id;
		break;
	}
	case ACTION_HEAL: {
		t.type = TRANSACTION_HEAL_CAST;
		t.phase = PHASE_ACTION;
		t.heal.caster_id = action.entity_id;
		t.heal.target_id = action.heal.target_id;
		t.heal.amount = action.heal.amount;
		break;
	}
	case ACTION_LIGHTNING: {
		t.type = TRANSACTION_LIGHTNING_CAST;
		t.phase = PHASE_ACTION;
		t.lightning.caster_id = action.entity_id;
		t.lightning.end = action.lightning.end;
		break;
	}
	case ACTION_SHOOT_WEB: {
		t.type = TRANSACTION_SHOOT_WEB_CAST;
		t.phase = PHASE_ACTION;
		t.shoot_web.caster_id = action.entity_id;
		t.shoot_web.target = action.shoot_web.target;
		break;
	}
	case ACTION_TURN_INVISIBLE: {
		t.type = TRANSACTION_TURN_INVISIBLE;
		t.phase = PHASE_ACTION;
		t.turn_invisible.entity_id = action.entity_id;
		break;
	}
	case ACTION_DRAW_CARDS: {
		t.type = TRANSACTION_DRAW_CARDS_1;
		t.phase = PHASE_CARDS;
		break;
	}
	case ACTION_DISCARD_HAND: {
		t.type = TRANSACTION_DISCARD_HAND;
		t.phase = PHASE_CARDS;
		t.start_time = constants.cards_ui.hand_to_discard_time;
		break;
	}
	case ACTION_PLAY_CARD: {
		t.type = TRANSACTION_PLAY_CARD;
		t.phase = PHASE_CARDS;
		t.play_card.card_id = action.play_card.card_id;
		Action card_action = {};
		card_action.type = action.play_card.action_type;
		card_action.entity_id = action.entity_id;
		for (u32 i = 0; i < action.play_card.params.len; ++i) {
			auto param = &action.play_card.params[i];
			switch (param->type) {
			case CARD_PARAM_TARGET:
				*(Pos*)((uptr)&card_action + param->offset) = param->target.dest;
				break;
			case CARD_PARAM_CREATURE:
				*(Entity_ID*)((uptr)&card_action + param->offset) = param->creature.id;
				break;
			case CARD_PARAM_AVAILABLE_TILE:
				*(Pos*)((uptr)&card_action + param->offset) = param->available_tile.dest;
				break;
			}
		}
		t.play_card.action = card_action;
		break;
	}
	}
	return t;
}

void game_simulate_actions(Game* game, Slice<Action> actions, Output_Buffer<Event> events)
{
	// TODO -- field of vision

	// =====================================================================
	// simulate actions

	u32 num_entities = game->entities.len;
	Entity *entities = game->entities.items;
	auto &tiles = game->tiles;

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

	Max_Length_Array<Transaction, GAME_MAX_TRANSACTIONS> transactions;
	transactions.reset();

	for (u32 i = 0; i < actions.len; ++i) {
		Action action = actions[i];
		transactions.append(to_transaction(action));
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
	auto cur_phase = (Phase)1;
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
					Entity *e = get_entity_by_id(game, entity_id);
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
					Entity *e = get_entity_by_id(game, entity_id);
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
					Entity *e = get_entity_by_id(game, entity_id);
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
			ASSERT(t->phase == PHASE_CURRENT || t->phase >= cur_phase);
			if (t->phase != PHASE_CURRENT && t->phase > cur_phase) {
				continue;
			}
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
				Entity *e = get_entity_by_id(game, t->move.entity_id);
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
				Entity *e = get_entity_by_id(game, t->move.entity_id);
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

				Entity *attacker = get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = get_entity_by_id(game, t->bump_attack.target_id);
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

				Entity *attacker = get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = get_entity_by_id(game, t->bump_attack.target_id);
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

			case TRANSACTION_STEAL_CARD: {
				t->type = TRANSACTION_REMOVE;
				ASSERT(t->steal_card.target_id == ENTITY_ID_PLAYER);

				auto stealer = get_entity_by_id(game, t->steal_card.stealer_id);
				auto target = get_entity_by_id(game, t->steal_card.target_id);

				if (!stealer || !target) {
					break;
				}

				Event event = {};
				event.type = EVENT_BUMP_ATTACK;
				event.time = time - constants.anims.bump_attack.duration / 2.0f;
				event.bump_attack.attacker_id = stealer->id;
				event.bump_attack.start = stealer->pos;
				event.bump_attack.end = target->pos;
				event.bump_attack.sound = SOUND_CARD_GAME_EFFECT_POOF_02;
				events.append(event);

				auto &deck = game->card_state.deck;
				auto &discard = game->card_state.discard;

				ASSERT(deck || discard);
				u32 idx = rand_u32() % (deck.len + discard.len);
				Card_ID card_id = 0;
				if (idx < deck.len) {
					card_id = deck[idx].id;
					deck.remove(idx);
				} else {
					idx -= deck.len;
					card_id = discard[idx].id;
					discard.remove(idx);
				}
				ASSERT(card_id);

				Event remove_card = {};
				remove_card.type = EVENT_REMOVE_CARD;
				remove_card.time = time;
				remove_card.remove_card.card_id = card_id;
				remove_card.remove_card.target_pos = target->pos;
				events.append(remove_card);

				break;
			}

			case TRANSACTION_BUMP_ATTACK_POISON_CONNECT: {
				// TODO -- move_pre_enter message
				//         move_post_enter message
				t->type = TRANSACTION_REMOVE;

				Entity *attacker = get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = get_entity_by_id(game, t->bump_attack.target_id);
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

				Entity *attacker = get_entity_by_id(game, t->bump_attack.attacker_id);
				Entity *target = get_entity_by_id(game, t->bump_attack.target_id);
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
				Entity *door = get_entity_by_id(game, door_id);
				if (!door) {
					break;
				}

				Event event = {};
				event.type = EVENT_OPEN_DOOR;
				event.time = time;
				event.open_door.door_id = door_id;
				event.open_door.new_appearance = APPEARANCE_DOOR_WOODEN_OPEN;
				events.append(event);
				door->flags = (Entity_Flag)(door->flags & ~ENTITY_FLAG_BLOCKS_VISION);
				door->block_mask = 0;
				door->default_action = ACTION_CLOSE_DOOR;

				break;
			}
			case TRANSACTION_CLOSE_DOOR: {
				t->type = TRANSACTION_REMOVE;

				auto door_id = t->close_door.door_id;
				auto door = get_entity_by_id(game, door_id);
				if (!door) {
					break;
				}

				Event event = {};
				event.type = EVENT_CLOSE_DOOR;
				event.time = time;
				event.close_door.door_id = door_id;
				event.close_door.new_appearance = door->appearance;
				events.append(event);

				door->flags = (Entity_Flag)(door->flags | ENTITY_FLAG_BLOCKS_VISION);
				door->block_mask = BLOCK_FLY | BLOCK_SWIM | BLOCK_WALK;
				door->default_action = ACTION_OPEN_DOOR;

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
				auto e = get_entity_by_id(game, t->fireball_shot.caster_id);
				if (!e) {
					t->type = TRANSACTION_REMOVE;
					break;
				}

				// TODO -- calculate sensible start time/duration for fireball
				// based on distance the fireball has to travel
				Event event = {};
				event.type = EVENT_FIREBALL_SHOT;
				event.time = time;
				event.fireball_shot.duration = constants.anims.fireball.shot_duration;
				event.fireball_shot.start = e->pos;
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
				projectile.entity_id = new_entity_id(game);
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

				Entity *a = get_entity_by_id(game, t->exchange.a);
				Entity *b = get_entity_by_id(game, t->exchange.b);

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

				Entity *e = get_entity_by_id(game, t->blink.caster_id);

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
				Entity *e = get_entity_by_id(game, e_id);
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

				Entity *e = get_entity_by_id(game, t->slime_split.slime_id);
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
						if (is_pos_passable(game, pend, BLOCK_WALK)) {
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
				Entity *caster = get_entity_by_id(game, t->fire_bolt.caster_id);
				Entity *target = get_entity_by_id(game, t->fire_bolt.target_id);
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
				Entity *target = get_entity_by_id(game, t->fire_bolt.target_id);
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
				Entity *caster = get_entity_by_id(game, t->heal.caster_id);
				if (!caster) {
					t->type = TRANSACTION_REMOVE;
					break;
				}
				t->heal.start = caster->pos;
				break;
			}
			case TRANSACTION_HEAL: {
				t->type = TRANSACTION_REMOVE;
				Entity *target = get_entity_by_id(game, t->heal.target_id);
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
				Entity *caster = get_entity_by_id(game, t->lightning.caster_id);
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
				projectile.entity_id = new_entity_id(game);
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
				auto caster = get_entity_by_id(game, t->shoot_web.caster_id);
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
				auto entity = get_entity_by_id(game, entity_id);

				entity->flags = (Entity_Flag)(ENTITY_FLAG_INVISIBLE | entity->flags);

				Event e = {};
				e.type = EVENT_TURN_INVISIBLE;
				e.time = t->start_time;
				e.turn_invisible.entity_id = entity_id;
				events.append(e);

				break;
			}

			case TRANSACTION_DRAW_CARDS_1: {
				t->type = TRANSACTION_DRAW_CARDS_2;
				u32 num_cards_to_draw = min_u32(game->card_state.hand_size, game->card_state.discard.len + game->card_state.deck.len);
				t->start_time += constants.cards_ui.draw_duration + (num_cards_to_draw - 1) * constants.cards_ui.between_draw_delay;
				t->draw_cards.num_cards_to_draw = num_cards_to_draw;
				break;
			}
			case TRANSACTION_DRAW_CARDS_2: {
				// TODO -- pre-draw/post-draw messages
				// post-draw message/poison implementation
				t->type = TRANSACTION_REMOVE;

				u32 num_cards_to_draw = t->draw_cards.num_cards_to_draw;

				auto card_state = &game->card_state;
				Event draw_card_event = {};
				draw_card_event.type = EVENT_DRAW_CARD;
				draw_card_event.time = t->start_time - (num_cards_to_draw - 1) * constants.cards_ui.between_draw_delay;

				for (u32 i = 0; i < num_cards_to_draw; ++i) {
					if (!card_state->deck) {
						while (card_state->discard) {
							u32 index = rand_u32() % card_state->discard.len;
							card_state->deck.append(card_state->discard[index]);
							card_state->discard.remove(index);
						}
						Event discard_to_deck_event = {};
						discard_to_deck_event.type = EVENT_SHUFFLE_DISCARD_TO_DECK;
						discard_to_deck_event.time = draw_card_event.time - constants.cards_ui.draw_duration;
						events.append(discard_to_deck_event);
					}
					auto card = card_state->deck.pop();
					card_state->hand.append(card);
					draw_card_event.card_draw.hand_index = i;
					draw_card_event.card_draw.card_id = card.id;
					events.append(draw_card_event);
					draw_card_event.time += constants.cards_ui.between_draw_delay;
				}

				break;
			}
			case TRANSACTION_DISCARD_HAND: {
				// TODO -- pre-discard/post-discard
				// make a disease card which refuses to be discarded!
				t->type = TRANSACTION_REMOVE;

				Event discard_hand_event = {};
				discard_hand_event.type = EVENT_DISCARD_HAND;
				discard_hand_event.time = t->start_time;
				events.append(discard_hand_event);

				Event discard_event = {};
				discard_event.type = EVENT_DISCARD;
				discard_event.time = t->start_time;

				auto card_state = &game->card_state;
				for (u32 i = 0; i < card_state->hand.len; ++i) {
					auto card = card_state->hand[i];
					card_state->discard.append(card);
					discard_event.discard.card_id = card.id;
					discard_event.discard.discard_index = card_state->discard.len - 1;
					events.append(discard_event);
				}
				card_state->hand.reset();

				for (u32 i = 0; i < card_state->in_play.len; ++i) {
					auto card = card_state->in_play[i];
					card_state->discard.append(card);
					discard_event.discard.card_id = card.id;
					discard_event.discard.discard_index = card_state->discard.len - 1;
					events.append(discard_event);
				}
				card_state->in_play.reset();

				break;
			}
			case TRANSACTION_PLAY_CARD: {
				// TODO -- messages (pre-play card, post-play card etc)
				t->type = TRANSACTION_REMOVE;

				Event play_card_event = {};
				play_card_event.type = EVENT_PLAY_CARD;
				play_card_event.time = t->start_time;
				play_card_event.play_card.card_id = t->play_card.card_id;
				events.append(play_card_event);

				auto card_state = &game->card_state;
				for (u32 i = 0; i < card_state->hand.len; ++i) {
					if (card_state->hand[i].id == t->play_card.card_id) {
						card_state->in_play.append(card_state->hand[i]);
						card_state->hand.remove_preserve_order(i);
						break;
					}
				}

				auto card_transaction = to_transaction(t->play_card.action);
				ASSERT(card_transaction.phase > PHASE_CARDS);
				transactions.append(card_transaction);

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
				remove_entity(game, entity_id);
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

		// maybe advance phase
		if (!physics_processing_left) {
			auto next_phase = NUM_PHASES;
			for (u32 i = 0; i < transactions.len; ++i) {
				auto t = &transactions[i];
				if (t->phase == PHASE_CURRENT) {
					next_phase = cur_phase;
					break;
				}
				if (t->phase < next_phase) {
					next_phase = t->phase;
				}
			}
			ASSERT(cur_phase <= next_phase && (next_phase < NUM_PHASES || transactions.len == 0));
			if (next_phase > cur_phase) {
				for (u32 i = 0; i < transactions.len; ++i) {
					auto t = &transactions[i];
					if (t->phase == next_phase) {
						t->start_time += time + TRANSACTION_EPSILON;
					}
				}
				cur_phase = next_phase;
			}
		}

		for (u32 i = 0; i < transactions.len; ++i) {
			Transaction *t = &transactions[i];
			ASSERT(t->phase == PHASE_CURRENT || t->phase >= cur_phase);
			if (t->phase != PHASE_CURRENT && t->phase > cur_phase) {
				continue;
			}
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

	Entity_ID player_id = get_player(game)->id;
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

void do_action(Game* game, Action action, Output_Buffer<Event> events)
{
	switch (action.type) {
	case ACTION_DRAW_CARDS:
	case ACTION_PLAY_CARD:
		game_simulate_actions(game, slice_one(&action), events);
		break;
	default: {
		auto c = &game->controllers[0];
		ASSERT(c->type == CONTROLLER_PLAYER);
		c->player.action = action;
		game_do_turn(game, events);
		break;
	}
	}
}

void get_card_params(Game* game, Card_ID card_id, Action_Type* action_type, Output_Buffer<Card_Param> card_params)
{
	ASSERT(action_type);
	Card *card = NULL;

	// XXX -- for now just look for the card in the hand, in future we'll have an
	// array of cards
	for (u32 i = 0; i < game->card_state.hand.len; ++i) {
		if (game->card_state.hand[i].id == card_id) {
			card = &game->card_state.hand[i];
			break;
		}
	}
	ASSERT(card);

	card_params.reset();
	switch (card->appearance) {
	case CARD_APPEARANCE_FIRE_MANA:
	case CARD_APPEARANCE_ROCK_MANA:
	case CARD_APPEARANCE_ELECTRIC_MANA:
	case CARD_APPEARANCE_AIR_MANA:
	case CARD_APPEARANCE_EARTH_MANA:
	case CARD_APPEARANCE_PSY_MANA:
	case CARD_APPEARANCE_WATER_MANA:
	case CARD_APPEARANCE_FIRE_SHIELD:
	case CARD_APPEARANCE_FIRE_WALL:
	case CARD_APPEARANCE_POISON:
	case CARD_APPEARANCE_MAGIC_MISSILE:
	case CARD_APPEARANCE_DISEASE:
		*action_type = ACTION_NONE;
		break;
	case CARD_APPEARANCE_FIREBALL: {
		*action_type = ACTION_FIREBALL;
		auto param = card_params.append();
		param->type = CARD_PARAM_TARGET;
		param->offset = OFFSET_OF(Action, fireball.end);
		break;
	}
	case CARD_APPEARANCE_FIRE_BOLT: {
		*action_type = ACTION_FIRE_BOLT;
		auto param = card_params.append();
		param->type = CARD_PARAM_CREATURE;
		param->offset = OFFSET_OF(Action, fire_bolt.target_id);
		break;
	}
	case CARD_APPEARANCE_EXCHANGE: {
		*action_type = ACTION_EXCHANGE;
		auto param = card_params.append();
		param->type = CARD_PARAM_CREATURE;
		param->offset = OFFSET_OF(Action, exchange.a);
		param = card_params.append();
		param->type = CARD_PARAM_CREATURE;
		param->offset = OFFSET_OF(Action, exchange.b);
		break;
	}
	case CARD_APPEARANCE_BLINK: {
		*action_type = ACTION_BLINK;
		auto param = card_params.append();
		param->type = CARD_PARAM_AVAILABLE_TILE;
		param->offset = OFFSET_OF(Action, blink.target);
		break;
	}
	case CARD_APPEARANCE_LIGHTNING: {
		*action_type = ACTION_LIGHTNING;
		auto param = card_params.append();
		param->type = CARD_PARAM_TARGET;
		param->offset = OFFSET_OF(Action, lightning.end);
		break;
	}
	}
}

Pos get_pos(Game* game, Entity_ID entity_id)
{
	auto e = get_entity_by_id(game, entity_id);
	if (e) {
		return e->pos;
	}
	if (entity_id < MAX_ENTITIES) {
		return Pos(0, 0);
	}
	return u16_to_pos(entity_id - MAX_ENTITIES);
}