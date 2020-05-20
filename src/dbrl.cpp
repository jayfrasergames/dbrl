#include "dbrl.h"

#include "tile_render.h"
#include "sprite_render.h"
#include "gen/background_tiles.data.h"
#include "gen/creature_sprites.data.h"

typedef v2_u8 Pos;

#define MAX_ENTITIES 10240

enum Appearance
{
	APPEARANCE_NONE,
	APPEARANCE_FLOOR,
	APPEARANCE_WALL,
	APPEARANCE_KNIGHT,
};

struct Entity
{
	Pos        pos;
	Appearance appearance;
};

struct Game
{
	u32 num_entities;
	Entity entities[MAX_ENTITIES];

	v2            screen_size;
	Sprite_Render creature_render;
	union {
		Sprite_Render_D3D11_Context creature_render_d3d11;
	};
	Tile_Render   bg_tiles;
	union {
		Tile_Render_D3D11_Context bg_tiles_d3d11;
	};
};

Memory_Spec get_game_size()
{
	Memory_Spec result = {};
	result.alignment = alignof(Game);
	result.size = sizeof(Game);
	return result;
}

void build_level_from_string(Game* game, char* str)
{
	Pos cur_pos = {};
	u32 idx = 0;
	for (char *p = str; *p; ++p) {
		switch (*p) {
		case '\n':
			cur_pos.x = 0;
			++cur_pos.y;
			continue;
		case '#':
			game->entities[idx++] = { cur_pos, APPEARANCE_WALL };
			break;
		case '.':
			game->entities[idx++] = { cur_pos, APPEARANCE_FLOOR };
			break;
		case '@':
			game->entities[idx++] = { cur_pos, APPEARANCE_FLOOR  };
			game->entities[idx++] = { cur_pos, APPEARANCE_KNIGHT };
		}
		++cur_pos.x;
	}
	game->num_entities = idx;
}

void init_game(Game* game)
{
	memset(game, 0, sizeof(*game));
	tile_render_init(&game->bg_tiles, (Tile_Render_Texture*)&TEXTURE_BACKGROUND_TILES.header);
	sprite_render_init(&game->creature_render,
	                   (Sprite_Render_Texture*)&TEXTURE_CREATURE_SPRITES.header);
	build_level_from_string(game,
		"###############\n"
		"#.............#\n"
		"#..@...#......#\n"
		"#.....###.....#\n"
		"#....#####....#\n"
		"#...##.#.##...#\n"
		"#..##..#..##..#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#......#......#\n"
		"#.............#\n"
		"###############\n");

	Tile_Render *bg_tiles = &game->bg_tiles;
	Sprite_Render *creatures = &game->creature_render;
	for (u32 idx = 0; idx < game->num_entities; ++idx) {
		Entity *e = &game->entities[idx];
		switch (e->appearance) {
		case APPEARANCE_NONE:
			break;
		case APPEARANCE_WALL: {
			Tile_Render_Tile_Instance instance = {};
			instance.world_pos  = { (f32)e->pos.x, (f32)e->pos.y };
			instance.sprite_pos = { 0.0f, 0.0f };
			tile_render_add_tile(bg_tiles, instance);
			break;
		}
		case APPEARANCE_FLOOR: {
			Tile_Render_Tile_Instance instance = {};
			instance.world_pos  = { (f32)e->pos.x, (f32)e->pos.y };
			instance.sprite_pos = { 3.0f, 0.0f };
			tile_render_add_tile(bg_tiles, instance);
			break;
		}
		case APPEARANCE_KNIGHT: {
			Sprite_Render_Sprite_Instance instance = {};
			instance.world_pos =  { (f32)e->pos.x, (f32)e->pos.y };
			instance.sprite_pos = { 0.0f, 0.0f };
			sprite_render_add_sprite(creatures, instance);
			break;
		}
		}
	}
}

u8 init_game_d3d11(Game* game, ID3D11Device* device)
{
	// TODO -- destroy these contexts in case of errors
	if (!tile_render_d3d11_init(&game->bg_tiles_d3d11, &game->bg_tiles, device)) {
		return 0;
	}
	if (!sprite_render_d3d11_init(&game->creature_render_d3d11, &game->creature_render, device)) {
		return 0;
	}
	return 1;
}

void process_frame(Game* game, Input* input, v2_u32 screen_size)
{
	game->screen_size = { (f32)screen_size.w, (f32)screen_size.h };
}

void render_d3d11(Game* game, ID3D11DeviceContext* dc, ID3D11RenderTargetView* output_rtv)
{
	tile_render_d3d11_draw(&game->bg_tiles_d3d11, &game->bg_tiles, dc, output_rtv, game->screen_size);
	sprite_render_d3d11_draw(&game->creature_render_d3d11, &game->creature_render, dc, output_rtv,
		game->screen_size);
}
