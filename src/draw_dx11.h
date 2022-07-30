#pragma once

#include "prelude.h"
#include "draw.h"
#include "jfg_d3d11.h"

// SHADER(name, filename, shader_model, entry_point)
#define DX11_PIXEL_SHADERS \
	DX11_PS(SPRITE_SHEET_RENDER_DXBC_PS, "sprite_sheet.hlsl", "ps_sprite") \
	DX11_PS(SPRITE_SHEET_FONT_DXBC_PS, "sprite_sheet.hlsl", "ps_font") \
	DX11_PS(PASS_THROUGH_PS, "pass_through_output.hlsl", "ps_pass_through") \
	DX11_PS(CARD_RENDER_DXBC_PS, "card_render.hlsl", "ps_card") \
	DX11_PS(DDW_TRIANGLE_DXBC_PS, "debug_draw_world.hlsl", "ps_triangle") \
	DX11_PS(DDW_LINE_DXBC_PS, "debug_draw_world.hlsl", "ps_line") \
	DX11_PS(FOV_EDGE_DXBC_PS, "field_of_vision_render.hlsl", "ps_edge") \
	DX11_PS(FOV_FILL_DXBC_PS, "field_of_vision_render.hlsl", "ps_fill")

#define DX11_VERTEX_SHADERS \
	DX11_VS(SPRITE_SHEET_RENDER_DXBC_VS, "sprite_sheet.hlsl", "vs_sprite") \
	DX11_VS(SPRITE_SHEET_FONT_DXBC_VS, "sprite_sheet.hlsl", "vs_font") \
	DX11_VS(PASS_THROUGH_VS, "pass_through_output.hlsl", "vs_pass_through") \
	DX11_VS(CARD_RENDER_DXBC_VS, "card_render.hlsl", "vs_card") \
	DX11_VS(DDW_TRIANGLE_DXBC_VS, "debug_draw_world.hlsl", "vs_triangle") \
	DX11_VS(DDW_LINE_DXBC_VS, "debug_draw_world.hlsl", "vs_line") \
	DX11_VS(FOV_EDGE_DXBC_VS, "field_of_vision_render.hlsl", "vs_edge") \
	DX11_VS(FOV_FILL_DXBC_VS, "field_of_vision_render.hlsl", "vs_fill")

#define DX11_COMPUTE_SHADERS \
	DX11_CS(SPRITE_SHEET_CLEAR_SPRITE_ID_CS, "sprite_sheet.hlsl", "cs_clear_sprite_id") \
	DX11_CS(SPRITE_SHEET_HIGHLIGHT_CS, "sprite_sheet.hlsl", "cs_highlight_sprite") \
	DX11_CS(PIXEL_ART_UPSAMPLER_CS, "pixel_art_upsampler.hlsl", "cs_pixel_art_upsampler") \
	DX11_CS(PARTICLES_DXBC_CS, "particles.hlsl", "cs_particles") \
	DX11_CS(FOV_BLEND_DXBC_CS, "field_of_vision_render.hlsl", "cs_shadow_blend") \
	DX11_CS(FOV_COMPOSITE_DXBC_CS, "field_of_vision_render.hlsl", "cs_composite")

enum DX11_Pixel_Shader
{
#define DX11_PS(name, ...) DX11_PS_##name,
	DX11_PIXEL_SHADERS
#undef DX11_PS

	NUM_DX11_PIXEL_SHADERS
};

enum DX11_Vertex_Shader
{
#define DX11_VS(name, ...) DX11_VS_##name,
	DX11_VERTEX_SHADERS
#undef DX11_VS

	NUM_DX11_VERTEX_SHADERS
};

enum DX11_Compute_Shader
{
#define DX11_CS(name, ...) DX11_CS_##name,
	DX11_COMPUTE_SHADERS
#undef DX11_CS

	NUM_DX11_COMPUTE_SHADERS
};

struct DX11_Renderer
{
	HMODULE              d3d11_library;

	ID3D11Device        *device;
	ID3D11DeviceContext *device_context;

	ID3D11PixelShader   *ps[NUM_DX11_PIXEL_SHADERS];
	ID3D11VertexShader  *vs[NUM_DX11_VERTEX_SHADERS];
	ID3D11ComputeShader *cs[NUM_DX11_COMPUTE_SHADERS];
};

bool init(DX11_Renderer* renderer);
void free(DX11_Renderer* renderer);

extern const Render_Functions dx11_render_functions;