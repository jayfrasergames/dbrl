#include "game.h"

#include "stdafx.h"
#include "random.h"

Entity* add_entity(Game* game)
{
	auto entity = game->entities.append();
	entity->id = game->next_entity_id++;
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

Entity* get_player(Game* game)
{
	return game_get_entity_by_id(game, game->player_id);
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
		if (entities[i].blocks_vision) {
			map.set(entities[i].pos);
		}
	}

	auto player = game_get_entity_by_id(game, game->player_id);
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

	auto c = add_controller(game);
	c->type = CONTROLLER_SPIDER_POISON;
	c->spider_normal.entity_id = e->id;

	return e;
}

Entity* add_spider_shadow(Game* game, Pos pos)
{
	auto e = add_enemy(game, 5);
	e->appearance = APPEARANCE_CREATURE_SPIDER_BLUE;
	e->movement_type = BLOCK_WALK;
	e->pos = pos;

	auto c = add_controller(game);
	c->type = CONTROLLER_SPIDER_SHADOW;
	c->spider_normal.entity_id = e->id;

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
	mh->type = MESSAGE_HANDLER_PREVENT_EXIT;
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
	Entity_ID entity_id = game->next_entity_id++;

	Entity e = {};
	e.hit_points = min_u32(hit_points, 5);
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


void make_bump_attacks(Controller* c, Game* game, Output_Buffer<Action> attacks)
{
	auto player = get_player(game);
	auto player_id = player->id;
	auto player_pos = player->pos;

	switch (c->type) {
	case CONTROLLER_PLAYER:
		if (c->player.action.type == ACTION_BUMP_ATTACK) {
			attacks.append(c->player.action);
			// entity_has_acted[player_id] = true;
		}
		break;
	case CONTROLLER_SLIME: {
		Entity *slime = game_get_entity_by_id(game, c->slime.entity_id);
		if (positions_are_adjacent(player_pos, slime->pos)) {
			Action bump = {};
			bump.type = ACTION_BUMP_ATTACK;
			bump.bump_attack.attacker_id = slime->id;
			bump.bump_attack.target_id = player_id;
			attacks.append(bump);
			// entity_has_acted[slime->id] = true;
		}
		break;
	}
	case CONTROLLER_LICH: {
		u32 num_skeletons = c->lich.skeleton_ids.len;
		for (u32 i = 0; i < num_skeletons; ++i) {
			Entity *skeleton = game_get_entity_by_id(game, c->lich.skeleton_ids[i]);
			if (positions_are_adjacent(player_pos, skeleton->pos)) {
				Action bump = {};
				bump.type = ACTION_BUMP_ATTACK;
				bump.entity_id = skeleton->id;
				bump.bump_attack.attacker_id = skeleton->id;
				bump.bump_attack.target_id = player_id;
				attacks.append(bump);
				// entity_has_acted[skeleton->id] = true;
			}
		}
		break;
	}
	case CONTROLLER_SPIDER_NORMAL: {
		auto spider_id = c->spider_normal.entity_id;
		auto spider = game_get_entity_by_id(game, spider_id);
		if (positions_are_adjacent(player_pos, spider->pos)) {
			Action bump = {};
			bump.type = ACTION_BUMP_ATTACK;
			bump.entity_id = spider_id;
			bump.bump_attack.attacker_id = spider_id;
			bump.bump_attack.target_id = player_id;
			attacks.append(bump);
		}
		break;
	}
	case CONTROLLER_SPIDER_WEB: {
		auto spider_id = c->spider_web.entity_id;
		auto spider = game_get_entity_by_id(game, spider_id);
		if (positions_are_adjacent(player_pos, spider->pos)) {
			Action bump = {};
			bump.type = ACTION_BUMP_ATTACK;
			bump.entity_id = spider_id;
			bump.bump_attack.attacker_id = spider_id;
			bump.bump_attack.target_id = player_id;
			attacks.append(bump);
		}
		break;
	}
	case CONTROLLER_SPIDER_POISON: {
		auto spider_id = c->spider_poison.entity_id;
		auto spider = game_get_entity_by_id(game, spider_id);
		if (positions_are_adjacent(player_pos, spider->pos)) {
			Action bump = {};
			bump.type = ACTION_BUMP_ATTACK;
			bump.entity_id = spider_id;
			bump.bump_attack.attacker_id = spider_id;
			bump.bump_attack.target_id = player_id;
			attacks.append(bump);
		}
		break;
	}
	case CONTROLLER_SPIDER_SHADOW: {
		auto spider_id = c->spider_shadow.entity_id;
		auto spider = game_get_entity_by_id(game, spider_id);
		if (positions_are_adjacent(player_pos, spider->pos)) {
			Action bump = {};
			bump.type = ACTION_BUMP_ATTACK;
			bump.entity_id = spider_id;
			bump.bump_attack.attacker_id = spider_id;
			bump.bump_attack.target_id = player_id;
			attacks.append(bump);
		}
		break;
	}

	case CONTROLLER_RANDOM_MOVE:
	case CONTROLLER_DRAGON:
		break;
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
		auto spider = game_get_entity_by_id(game, spider_id);
		auto player = get_player(game);

		v2_i16 spider_pos = (v2_i16)spider->pos;
		v2_i16 player_pos = (v2_i16)player->pos;
		v2_i16 d = player_pos - spider_pos;
		u32 cur_dist_squared = d.x*d.x + d.y*d.y;

		for (i16 dy = -1; dy <= 1; ++dy) {
			for (i16 dx = -1; dx <= 1; ++dx) {
				if (!dx && !dy) {
					continue;
				}
				v2_i16 end = spider_pos + v2_i16(dx, dy);
				d = player_pos - end;
				Tile t = tiles[(Pos)end];
				if (d.x*d.x + d.y*d.y <= cur_dist_squared
				 && tile_is_passable(t, spider->movement_type)) {
					Potential_Move pm = {};
					pm.entity_id = spider_id;
					pm.start = (Pos)spider_pos;
					pm.end = (Pos)end;
					pm.weight = uniform_f32(1.9f, 2.1f);
					potential_moves.append(pm);
				}
			}
		}
		break;
	}

	case CONTROLLER_SPIDER_WEB:
	case CONTROLLER_SPIDER_POISON:
	case CONTROLLER_SPIDER_SHADOW:
		break;

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
		Entity *e = game_get_entity_by_id(game, c->dragon.entity_id);
		u16 move_mask = e->movement_type;
		u32 t_idx = rand_u32() % game->entities.len;
		Entity *t = &game->entities[t_idx];
		Action a = {};
		a.type = ACTION_FIREBALL;
		a.entity_id = c->dragon.entity_id;
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
			a.entity_id = c->lich.lich_id;
			a.heal.caster_id = c->lich.lich_id;
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
		if (has_acted[spider_id]) {
			return;
		}
		if (c->spider_web.web_cooldown) {
			--c->spider_web.web_cooldown;
			return;
		}

		auto player = get_player(game);
		auto spider = game_get_entity_by_id(game, spider_id);

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

	}
}