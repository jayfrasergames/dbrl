#include "dbrl.h"

#include "jfg/imgui.h"
#include "jfg/jfg_math.h"
#include "sprite_sheet.h"
#include "pixel_art_upsampler.h"

#include <stdio.h>  // XXX - for snprintf
#include <stdlib.h> // XXX - for random

#include "gen/appearance.data.h"
#include "gen/sprite_sheet_creatures.data.h"
#include "gen/sprite_sheet_tiles.data.h"
#include "gen/sprite_sheet_water_edges.data.h"

#include "gen/pass_through_dxbc_vertex_shader.data.h"
#include "gen/pass_through_dxbc_pixel_shader.data.h"

// =============================================================================
// type definitions/constants

typedef v2_u8 Pos;

#define MAX_ENTITIES 10240

typedef u16 Entity_ID;

// =============================================================================
// game

struct Entity
{
	Entity_ID  id;
	Pos        pos;
	Appearance appearance;
};

struct Game_State
{
	u32 num_entities;
	Entity entities[MAX_ENTITIES];
};

void game_build_from_string(Game_State* game, char* str)
{
	Pos cur_pos = { 1, 1 };
	u32 idx = 0;
	Entity_ID e_id = 1;
	for (char *p = str; *p; ++p) {
		switch (*p) {
		case '\n':
			cur_pos.x = 1;
			++cur_pos.y;
			continue;
		case '#':
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_WALL_WOOD };
			break;
		case 'x':
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_WALL_FANCY };
			break;
		case '.':
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_FLOOR_ROCK };
			break;
		case '@':
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_FLOOR_ROCK };
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_CREATURE_MALE_WIZARD };
			break;
		case 'b':
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_FLOOR_ROCK };
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_CREATURE_BLACK_BAT };
			break;
		case '~':
			game->entities[idx++] = { e_id++, cur_pos, APPEARANCE_LIQUID_WATER };
			break;
		}
		++cur_pos.x;
	}
	game->num_entities = idx;
}

// =============================================================================
// draw

struct Camera
{
	v2  world_center;
	f32 zoom;
};

struct Draw
{
	Camera camera;
	Sprite_Sheet_Renderer  renderer;
	Sprite_Sheet_Instances tiles;
	Sprite_Sheet_Instances creatures;
	Sprite_Sheet_Instances water_edges;
};

// =============================================================================
// anim

enum Anim_Type
{
	ANIM_TILE_STATIC,
	ANIM_WATER_EDGE,
	ANIM_CREATURE_IDLE,
};

struct Anim
{
	Anim_Type type;
	union {
		struct {
			f32 duration;
			f32 offset;
		} idle;
		struct {
			v4_u8 color;
		} water_edge;
	};
	v2 sprite_coords;
	v2 world_coords;
	Entity_ID entity_id;
	f32 depth_offset;
};

#define MAX_ANIMS (5 * MAX_ENTITIES)

struct World_Anim_State
{
	u32  num_anims;
	Anim anims[MAX_ANIMS];
};

void world_anim_init(World_Anim_State* world_anim, Game_State* game)
{
	u32 anim_idx = 0;

	u8 wall_id_grid[65536];
	memset(wall_id_grid, 0, sizeof(wall_id_grid));
	Entity_ID wall_entity_id_grid[65536];

	u8 liquid_id_grid[65536];
	memset(liquid_id_grid, 0, sizeof(liquid_id_grid));
	Entity_ID liquid_entity_id_grid[65536];

	for (u32 i = 0; i < game->num_entities; ++i) {
		Entity *e = &game->entities[i];
		Appearance app = e->appearance;
		if (!app) {
			continue;
		}
		Pos pos = e->pos;
		if (appearance_is_creature(app)) {
			Anim ca = {};
			ca.type = ANIM_CREATURE_IDLE;
			ca.sprite_coords = appearance_get_creature_sprite_coords(app);
			ca.world_coords = { (f32)pos.x, (f32)pos.y };
			ca.entity_id = e->id;
			ca.depth_offset = 1.0f;
			ca.idle.duration = 0.8f + 0.4f * ((f32)rand() / (f32)RAND_MAX);
			ca.idle.offset = 0.0f;
			world_anim->anims[anim_idx++] = ca;
			continue;
		}
		if (appearance_is_floor(app)) {
			Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_floor_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = 0.0f;
			world_anim->anims[anim_idx++] = ta;
			continue;
		}
		if (appearance_is_wall(app)) {
			u8 wall_id = appearance_get_wall_id(app);
			u32 index = pos.y * 256 + pos.x;
			wall_id_grid[index] = wall_id;
			wall_entity_id_grid[index] = e->id;
			continue;
		}
		if (appearance_is_liquid(app)) {
			Anim ta = {};
			ta.type = ANIM_TILE_STATIC;
			ta.sprite_coords = appearance_get_liquid_sprite_coords(app);
			ta.world_coords = { (f32)pos.x, (f32)pos.y };
			ta.entity_id = e->id;
			ta.depth_offset = 0.0f;
			world_anim->anims[anim_idx++] = ta;
			u32 index = pos.y * 256 + pos.x;
			liquid_id_grid[index] = appearance_get_liquid_id(app);
			liquid_entity_id_grid[index] = e->id;
		}
	}

	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			u32 index = y*256 + x;
			u8 c = wall_id_grid[index];
			if (!c) {
				continue;
			}
			Appearance app = appearance_wall_id_to_appearance(c);

			// get whether the floor ids equal c in the following pattern
			//  tl | t | tr
			// ----+---+----
			//  l  | c | r
			// ----+---+----
			//  bl | b | br

			u8 tl = wall_id_grid[index - 257] == c;
			u8 t  = wall_id_grid[index - 256] == c;
			u8 tr = wall_id_grid[index - 255] == c;
			u8 l  = wall_id_grid[index - 1]  == c;
			u8 r  = wall_id_grid[index + 1]  == c;
			u8 bl = wall_id_grid[index + 255] == c;
			u8 b  = wall_id_grid[index + 256] == c;
			u8 br = wall_id_grid[index + 257] == c;

			u8 connection_mask = 0;
			if (t && !(tl && tr && l && r)) {
				connection_mask |= APPEARANCE_N;
			}
			if (r && !(t && tr && b && br)) {
				connection_mask |= APPEARANCE_E;
			}
			if (b && !(l && r && bl && br)) {
				connection_mask |= APPEARANCE_S;
			}
			if (l && !(t && b && tl && bl)) {
				connection_mask |= APPEARANCE_W;
			}

			Anim anim = {};
			anim.type = ANIM_TILE_STATIC;
			anim.sprite_coords = appearance_get_wall_sprite_coords(app, connection_mask);
			anim.world_coords = { (f32)x, (f32)y };
			anim.entity_id = wall_entity_id_grid[index];
			anim.depth_offset = 0.0f;
			world_anim->anims[anim_idx++] = anim;
		}
	}

	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			u32 index = y*256 + x;
			u8 c = liquid_id_grid[index];

			if (!c) {
				continue;
			}

			u8 mask = 0;
			if (liquid_id_grid[index - 256] == c) { mask |= 0x01; }
			if (liquid_id_grid[index - 255] == c) { mask |= 0x02; }
			if (liquid_id_grid[index +   1] == c) { mask |= 0x04; }
			if (liquid_id_grid[index + 257] == c) { mask |= 0x08; }
			if (liquid_id_grid[index + 256] == c) { mask |= 0x10; }
			if (liquid_id_grid[index + 255] == c) { mask |= 0x20; }
			if (liquid_id_grid[index -   1] == c) { mask |= 0x40; }
			if (liquid_id_grid[index - 257] == c) { mask |= 0x80; }
			if (~mask & 0xFF) {
				Anim anim = {};
				anim.type = ANIM_WATER_EDGE;
				anim.sprite_coords = { (f32)(mask % 16), (f32)(15 - mask / 16) };
				anim.world_coords = { (f32)x, (f32)y };
				anim.entity_id = liquid_entity_id_grid[index];
				anim.depth_offset = 0.1f;
				anim.water_edge.color = { 0x58, 0x80, 0xc0, 255 };
				world_anim->anims[anim_idx++] = anim;
			}
		}
	}

	ASSERT(anim_idx < MAX_ANIMS);
	world_anim->num_anims = anim_idx;
}

void world_anim_draw(World_Anim_State* world_anim, Draw* draw, f32 time)
{
	// reset draw state
	sprite_sheet_instances_reset(&draw->tiles);
	sprite_sheet_instances_reset(&draw->creatures);
	sprite_sheet_instances_reset(&draw->water_edges);

	// draw tile animations
	for (u32 i = 0; i < world_anim->num_anims; ++i) {
		Anim *anim = &world_anim->anims[i];
		switch (anim->type) {
		case ANIM_TILE_STATIC: {
			Sprite_Sheet_Instance ti = {};
			ti.sprite_pos = anim->sprite_coords;
			ti.world_pos = anim->world_coords;
			ti.sprite_id = anim->entity_id;
			ti.depth_offset = anim->depth_offset;
			ti.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			sprite_sheet_instances_add(&draw->tiles, ti);
			break;
		}
		case ANIM_CREATURE_IDLE: {
			Sprite_Sheet_Instance ci = {};

			v2 world_pos = anim->world_coords;
			world_pos.y -= 3.0f / 24.0f;
			ci.sprite_pos = { 4.0f, 22.0f };
			ci.world_pos = world_pos;
			ci.sprite_id = anim->entity_id;
			ci.depth_offset = anim->depth_offset;
			ci.color_mod = { 1.0f, 1.0f, 1.0f, 1.0f };
			sprite_sheet_instances_add(&draw->creatures, ci);

			v2 sprite_pos = anim->sprite_coords;
			f32 dt = time + anim->idle.offset;
			dt = fmod(dt, anim->idle.duration) / anim->idle.duration;
			if (dt > 0.5f) {
				sprite_pos.y += 1.0f;
			}
			ci.sprite_pos = sprite_pos;
			world_pos.y -= 3.0f / 24.0f;
			ci.world_pos = world_pos;
			ci.depth_offset += 1.0f;

			sprite_sheet_instances_add(&draw->creatures, ci);
			break;
		}
		case ANIM_WATER_EDGE: {
			Sprite_Sheet_Instance water_edge = {};
			water_edge.sprite_pos = anim->sprite_coords;
			water_edge.world_pos = anim->world_coords;
			water_edge.sprite_id = anim->entity_id;
			water_edge.depth_offset = anim->depth_offset;
			v4_u8 c = anim->water_edge.color;
			water_edge.color_mod = {
				(f32)c.r / 256.0f,
				(f32)c.g / 256.0f,
				(f32)c.b / 256.0f,
				(f32)c.a / 256.0f,
			};
			sprite_sheet_instances_add(&draw->water_edges, water_edge);
			break;
		}
		}
	}
}

// =============================================================================
// program

enum Program_Input_State
{
	GIS_NONE,
	GIS_DRAGGING_MAP,
};

struct Program
{
	Game_State          game;
	Program_Input_State program_input_state;
	World_Anim_State    world_anim;
	Draw                draw;

	u32           frame_number;
	v2            screen_size;
	v2_u32        max_screen_size;
	IMGUI_Context imgui;

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

Memory_Spec get_program_size()
{
	Memory_Spec result = {};
	result.alignment = alignof(Program);
	result.size = sizeof(Program);
	return result;
}

void program_init(Program* program)
{
	memset(program, 0, sizeof(*program));
	sprite_sheet_renderer_init(&program->draw.renderer,
	                           &program->draw.tiles, 3,
	                           { 1600, 900 });

	program->draw.tiles.data       = SPRITE_SHEET_TILES;
	program->draw.creatures.data   = SPRITE_SHEET_CREATURES;
	program->draw.water_edges.data = SPRITE_SHEET_WATER_EDGES;

	game_build_from_string(&program->game,
		"##########################################\n"
		"#.............#..........................#\n"
		"#..@...#...b..#....b.....................#\n"
		"#.....###.....#.........###..............#\n"
		"#....#####....#.........#x#.......b......#\n"
		"#...##.#.##...#.......###x#.#............#\n"
		"#..##..#..##..#.......#xxxxx#............#\n"
		"#......#......#.......###x###............#\n"
		"#......#......#.........#x#..............#\n"
		"#...b..#...b............###..............#\n"
		"#......#......#..........................#\n"
		"#......#......#....b.......#.............#\n"
		"#......#......#..........................#\n"
		"#~~~...#.....x#..............#...........#\n"
		"#~~~~..#.....x#..........................#\n"
		"#~~~~~~..xxxxx#............#...#.........#\n"
		"##########x####..........................#\n"
		"#.........x...#........#.................#\n"
		"#......b..x...#.......##.................#\n"
		"#.............#......##.##...............#\n"
		"#.............#.......##.................#\n"
		"#.............#........#.................#\n"
		"#...b......b.................b...........#\n"
		"#.............#..........................#\n"
		"#.............#...b...............b......#\n"
		"#.............#..........................#\n"
		"#......b......#..........................#\n"
		"#.............#...b......................#\n"
		"#.............#..............b...........#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"#.............#..........................#\n"
		"##########################################\n");

	world_anim_init(&program->world_anim, &program->game);
	program->draw.camera.zoom = 4.0f;
	program->draw.camera.world_center = { 0.0f, 0.0f };
}

u8 program_d3d11_init(Program* program, ID3D11Device* device, v2_u32 screen_size)
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

	if (!sprite_sheet_renderer_d3d11_init(&program->draw.renderer, device)) {
		goto error_init_sprite_sheet_renderer;
	}

	if (!sprite_sheet_instances_d3d11_init(&program->draw.tiles, device)) {
		goto error_init_sprite_sheet_tiles;
	}

	if (!sprite_sheet_instances_d3d11_init(&program->draw.creatures, device)) {
		goto error_init_sprite_sheet_creatures;
	}

	if (!sprite_sheet_instances_d3d11_init(&program->draw.water_edges, device)) {
		goto error_init_sprite_sheet_water_edges;
	}

	if (!pixel_art_upsampler_d3d11_init(&program->pixel_art_upsampler, device)) {
		goto error_init_pixel_art_upsampler;
	}

	if (!imgui_d3d11_init(&program->imgui, device)) {
		goto error_init_imgui;
	}

	program->max_screen_size      = screen_size;
	program->d3d11.output_texture = output_texture;
	program->d3d11.output_uav     = output_uav;
	program->d3d11.output_rtv     = output_rtv;
	program->d3d11.output_srv     = output_srv;
	program->d3d11.output_vs      = output_vs;
	program->d3d11.output_ps      = output_ps;

	return 1;

	imgui_d3d11_free(&program->imgui);
error_init_imgui:
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
error_init_pixel_art_upsampler:
	sprite_sheet_instances_d3d11_free(&program->draw.water_edges);
error_init_sprite_sheet_water_edges:
	sprite_sheet_instances_d3d11_free(&program->draw.creatures);
error_init_sprite_sheet_creatures:
	sprite_sheet_instances_d3d11_free(&program->draw.tiles);
error_init_sprite_sheet_tiles:
	sprite_sheet_renderer_d3d11_free(&program->draw.renderer);
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

void program_d3d11_free(Program* program)
{
	imgui_d3d11_free(&program->imgui);
	pixel_art_upsampler_d3d11_free(&program->pixel_art_upsampler);
	sprite_sheet_instances_d3d11_free(&program->draw.water_edges);
	sprite_sheet_instances_d3d11_free(&program->draw.creatures);
	sprite_sheet_instances_d3d11_free(&program->draw.tiles);
	sprite_sheet_renderer_d3d11_free(&program->draw.renderer);
	program->d3d11.output_ps->Release();
	program->d3d11.output_vs->Release();
	program->d3d11.output_srv->Release();
	program->d3d11.output_rtv->Release();
	program->d3d11.output_uav->Release();
	program->d3d11.output_texture->Release();
}

u8 program_d3d11_set_screen_size(Program* program, ID3D11Device* device, v2_u32 screen_size)
{
	v2_u32 prev_max_screen_size = program->max_screen_size;
	if (screen_size.w < prev_max_screen_size.w && screen_size.h < prev_max_screen_size.h) {
		return 1;
	}
	screen_size.w = max_u32(screen_size.w, prev_max_screen_size.w);
	screen_size.h = max_u32(screen_size.h, prev_max_screen_size.h);

	HRESULT hr;
	ID3D11Texture2D *output_texture;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = screen_size.w;
		desc.Height = screen_size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
		                                            | D3D11_BIND_UNORDERED_ACCESS;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

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

	program->d3d11.output_uav->Release();
	program->d3d11.output_rtv->Release();
	program->d3d11.output_srv->Release();
	program->d3d11.output_texture->Release();

	program->max_screen_size = screen_size;
	program->d3d11.output_uav = output_uav;
	program->d3d11.output_rtv = output_rtv;
	program->d3d11.output_srv = output_srv;
	program->d3d11.output_texture = output_texture;
	return 1;

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

static v2_i32 screen_pos_to_world_pos(Camera* camera, v2_u32 screen_size, v2_u32 screen_pos)
{
	v2_i32 world_pos = {};
	v2_i32 p = { (i32)screen_pos.x - (i32)screen_size.w / 2,
	             (i32)screen_pos.y - (i32)screen_size.h / 2 };
	world_pos.x = (i32)((f32)p.x / camera->zoom - 24.0f * camera->world_center.x);
	world_pos.y = (i32)((f32)p.y / camera->zoom - 24.0f * camera->world_center.y);
	return world_pos;
}

void process_frame(Program* program, Input* input, v2_u32 screen_size)
{
	program->screen_size = { (f32)screen_size.w, (f32)screen_size.h };
	++program->frame_number;

	// process input
	switch (program->program_input_state) {
	case GIS_NONE:
		if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_ENDED_DOWN) {
			program->program_input_state = GIS_DRAGGING_MAP;
		}
		break;
	case GIS_DRAGGING_MAP:
		if (input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags & INPUT_BUTTON_FLAG_HELD_DOWN) {
			f32 zoom = 24.0f * program->draw.camera.zoom;
			program->draw.camera.world_center.x += (f32)input->mouse_delta.x / zoom;
			program->draw.camera.world_center.y += (f32)input->mouse_delta.y / zoom;
		} else if (!(input->button_data[INPUT_BUTTON_MOUSE_MIDDLE].flags
		             & INPUT_BUTTON_FLAG_ENDED_DOWN)) {
			program->program_input_state = GIS_NONE;
		}
		break;
	}
	program->draw.camera.zoom += (f32)input->mouse_wheel_delta * 0.25f;

	f32 time = (f32)program->frame_number / 60.0f;
	world_anim_draw(&program->world_anim, &program->draw, time);

	// do stuff
	imgui_begin(&program->imgui);
	imgui_set_text_cursor(&program->imgui, { 1.0f, 0.0f, 1.0f, 1.0f }, { 5.0f, 5.0f });
	char buffer[1024];
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse Pos: (%u, %u)",
	         input->mouse_pos.x, input->mouse_pos.y);
	imgui_text(&program->imgui, buffer);
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse Delta: (%d, %d)",
	         input->mouse_delta.x, input->mouse_delta.y);
	imgui_text(&program->imgui, buffer);
	v2_i32 world_mouse_pos = screen_pos_to_world_pos(&program->draw.camera,
	                                                 screen_size,
	                                                 input->mouse_pos);
	snprintf(buffer, ARRAY_SIZE(buffer), "Mouse world pos: (%d, %d)",
	         world_mouse_pos.x, world_mouse_pos.y);
	imgui_text(&program->imgui, buffer);
	u32 sprite_id = sprite_sheet_renderer_id_in_pos(&program->draw.renderer,
	                                                { (u32)world_mouse_pos.x,
	                                                  (u32)world_mouse_pos.y });
	sprite_sheet_renderer_highlight_sprite(&program->draw.renderer, sprite_id);
	snprintf(buffer, ARRAY_SIZE(buffer), "Sprite ID: %u", sprite_id);
	imgui_text(&program->imgui, buffer);
}

void render_d3d11(Program* program, ID3D11DeviceContext* dc, ID3D11RenderTargetView* output_rtv)
{
	v2_u32 screen_size_u32 = { (u32)program->screen_size.x, (u32)program->screen_size.y };

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(program->d3d11.output_rtv, clear_color);

	sprite_sheet_renderer_d3d11_begin(&program->draw.renderer, dc);
	sprite_sheet_instances_d3d11_draw(&program->draw.tiles, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.creatures, dc, screen_size_u32);
	sprite_sheet_instances_d3d11_draw(&program->draw.water_edges, dc, screen_size_u32);
	sprite_sheet_renderer_d3d11_end(&program->draw.renderer, dc);

	f32 zoom = program->draw.camera.zoom;
	/* v2_u32 input_size = { (u32)(((f32)screen_size_u32.w) / zoom),
	                      (u32)(((f32)screen_size_u32.h) / zoom) }; */
	v2_i32 world_tl = screen_pos_to_world_pos(&program->draw.camera,
	                                          screen_size_u32,
	                                          { 0, 0 });
	v2_i32 world_br = screen_pos_to_world_pos(&program->draw.camera,
	                                          screen_size_u32,
	                                          screen_size_u32);
	v2_u32 input_size = { (u32)(world_br.x - world_tl.x), (u32)(world_br.y - world_tl.y) };
	pixel_art_upsampler_d3d11_draw(&program->pixel_art_upsampler,
	                               dc,
	                               program->draw.renderer.d3d11.output_srv,
	                               program->d3d11.output_uav,
	                               input_size,
	                               world_tl,
	                               screen_size_u32,
	                               { 0, 0 });

	imgui_d3d11_draw(&program->imgui, dc, program->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(program->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(program->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &program->d3d11.output_srv);
	dc->Draw(6, 0);
}
