#include "dbrl.h"

#include "jfg/imgui.h"
#include "sprite_sheet.h"
#include "pixel_art_upsampler.h"

// XXX - tmp for snprintf
#include <stdio.h>

#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"

#include "gen/pass_through_dxbc_vertex_shader.data.h"
#include "gen/pass_through_dxbc_pixel_shader.data.h"

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

	u32           frame_number;
	v2            screen_size;
	IMGUI_Context imgui;
	union {
		IMGUI_D3D11_Context imgui_d3d11;
	};
	Sprite_Sheet_Renderer  sprite_sheet_renderer;
	Sprite_Sheet_Instances tiles;
	Sprite_Sheet_Instances creatures;
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

void game_init(Game* game)
{
	memset(game, 0, sizeof(*game));
	sprite_sheet_renderer_init(&game->sprite_sheet_renderer, { 1600, 900 });
	game->tiles.data     = SPRITE_SHEET_TILES;
	game->creatures.data = SPRITE_SHEET_CREATURES;
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

	Sprite_Sheet_Instances *bg_tiles = &game->tiles;
	Sprite_Sheet_Instances *creatures = &game->creatures;
	for (u32 idx = 0; idx < game->num_entities; ++idx) {
		Entity *e = &game->entities[idx];
		switch (e->appearance) {
		case APPEARANCE_NONE:
			break;
		case APPEARANCE_WALL: {
			Sprite_Sheet_Instance instance = {};
			instance.world_pos    = { (f32)e->pos.x, (f32)e->pos.y };
			instance.sprite_pos   = { 0.0f, 0.0f };
			instance.depth_offset = 1.0f;
			sprite_sheet_instances_add(bg_tiles, instance);
			break;
		}
		case APPEARANCE_FLOOR: {
			Sprite_Sheet_Instance instance = {};
			instance.world_pos    = { (f32)e->pos.x, (f32)e->pos.y };
			instance.sprite_pos   = { 3.0f, 0.0f };
			instance.depth_offset = 0.0f;
			sprite_sheet_instances_add(bg_tiles, instance);
			break;
		}
		case APPEARANCE_KNIGHT: {
			Sprite_Sheet_Instance instance = {};
			instance.world_pos    = { (f32)e->pos.x, (f32)e->pos.y };
			instance.sprite_pos   = { 0.0f, 0.0f };
			instance.depth_offset = 1.0f;
			sprite_sheet_instances_add(creatures, instance);
			break;
		}
		}
	}
}

u8 game_d3d11_init(Game* game, ID3D11Device* device, v2_u32 screen_size)
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

	if (!sprite_sheet_renderer_d3d11_init(&game->sprite_sheet_renderer, device)) {
		goto error_init_sprite_sheet_renderer;
	}

	if (!sprite_sheet_instances_d3d11_init(&game->tiles, device)) {
		goto error_init_sprite_sheet_tiles;
	}

	if (!sprite_sheet_instances_d3d11_init(&game->creatures, device)) {
		goto error_init_sprite_sheet_creatures;
	}

	if (!pixel_art_upsampler_d3d11_init(&game->pixel_art_upsampler, device)) {
		goto error_init_pixel_art_upsampler;
	}

	// XXX - should have an IMGUI free call
	if (!imgui_d3d11_init(&game->imgui_d3d11, device)) {
		return 0;
	}

	game->d3d11.output_texture = output_texture;
	game->d3d11.output_uav     = output_uav;
	game->d3d11.output_rtv     = output_rtv;
	game->d3d11.output_srv     = output_srv;
	game->d3d11.output_vs      = output_vs;
	game->d3d11.output_ps      = output_ps;

	return 1;

	pixel_art_upsampler_d3d11_free(&game->pixel_art_upsampler);
error_init_pixel_art_upsampler:
	sprite_sheet_instances_d3d11_free(&game->creatures);
error_init_sprite_sheet_creatures:
	sprite_sheet_instances_d3d11_free(&game->tiles);
error_init_sprite_sheet_tiles:
	sprite_sheet_renderer_d3d11_free(&game->sprite_sheet_renderer);
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

void game_d3d11_free(Game* game)
{
}

void process_frame(Game* game, Input* input, v2_u32 screen_size)
{
	game->screen_size = { (f32)screen_size.w, (f32)screen_size.h };
	++game->frame_number;

	imgui_begin(&game->imgui);
	imgui_set_text_cursor(&game->imgui, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
	char buffer[1024];
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse Pos: (%u, %u)",
		input->mouse_pos.x, input->mouse_pos.y);
	imgui_text(&game->imgui, buffer);
	u32 sprite_id = sprite_sheet_instances_id_in_pos(&game->tiles,
	                                                 { input->mouse_pos.x / 4,
	                                                   input->mouse_pos.y / 4 });
	snprintf(buffer, ARRAY_SIZE(buffer), "Sprite ID: %u", sprite_id);
	imgui_text(&game->imgui, buffer);
}

void render_d3d11(Game* game, ID3D11DeviceContext* dc, ID3D11RenderTargetView* output_rtv)
{
	v2_u32 screen_size_u32 = { (u32)game->screen_size.x, (u32)game->screen_size.y };

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(game->d3d11.output_rtv, clear_color);

	sprite_sheet_renderer_d3d11_begin(&game->sprite_sheet_renderer, dc);
	sprite_sheet_instances_d3d11_draw(&game->tiles, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&game->creatures, dc, screen_size_u32);
	sprite_sheet_renderer_d3d11_end(&game->sprite_sheet_renderer, dc);

	f32 zoom = 4.0f;
	v2_u32 input_size = { (u32)(((f32)screen_size_u32.w) / zoom),
	                      (u32)(((f32)screen_size_u32.h) / zoom) };
	pixel_art_upsampler_d3d11_draw(&game->pixel_art_upsampler,
	                               dc,
	                               game->sprite_sheet_renderer.d3d11.output_srv,
	                               game->d3d11.output_uav,
	                               input_size,
	                               {                     0,                     0 },
	                               {     screen_size_u32.w,     screen_size_u32.h },
	                               {                     0,                     0 });

	imgui_d3d11_draw(&game->imgui, &game->imgui_d3d11, dc, game->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(game->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(game->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &game->d3d11.output_srv);
	dc->Draw(6, 0);
}
