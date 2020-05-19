#include "dbrl.h"

#include "tile_render.h"
#include "gen/background_tiles.data.h"

struct Game
{
	v2 screen_size;
	Tile_Render bg_tiles;
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

void init_game(Game* game)
{
	tile_render_init(&game->bg_tiles, (Tile_Render_Texture*)&TEXTURE_BACKGROUND_TILES.header);
	Tile_Render_Tile_Instance instance = {};
	instance.sprite_pos.x = 10.0f;
	instance.sprite_pos.y = 10.0f;
	for (u32 i = 0; i < 10; ++i) {
		for (u32 j = 0; j < 10; ++j) {
			instance.world_pos.x = (f32)i;
			instance.world_pos.y = (f32)j;
			tile_render_add_tile(&game->bg_tiles, instance);
		}
	}
}

u8 init_game_d3d11(Game* game, ID3D11Device* device)
{
	if (!tile_render_d3d11_init(&game->bg_tiles_d3d11, &game->bg_tiles, device)) {
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
}
