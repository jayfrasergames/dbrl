#include "game.h"

#include "stdafx.h"

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

Entity_ID add_slime(Game *game, Pos pos, u32 hit_points)
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
