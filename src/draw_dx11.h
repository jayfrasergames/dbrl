#pragma once

#include "prelude.h"
#include "containers.hpp"

#include "draw.h"
#include "jfg_d3d11.h"
#include <dxgi1_2.h>

#include "pixel_art_upsampler.h"

#include "render.h"

// SHADER(name, filename, shader_model, entry_point)
#define DX11_PIXEL_SHADERS \
	DX11_PS(SPRITE_SHEET_RENDER, "sprite_sheet.hlsl",           "ps_sprite") \
	DX11_PS(SPRITE_SHEET_FONT,   "sprite_sheet.hlsl",           "ps_font") \
	DX11_PS(PASS_THROUGH,        "pass_through_output.hlsl",    "ps_pass_through") \
	DX11_PS(CARD_RENDER,         "card_render.hlsl",            "ps_card") \
	DX11_PS(DDW_TRIANGLE,        "debug_draw_world.hlsl",       "ps_triangle") \
	DX11_PS(DDW_LINE,            "debug_draw_world.hlsl",       "ps_line") \
	DX11_PS(FOV_EDGE,            "field_of_vision_render.hlsl", "ps_edge") \
	DX11_PS(FOV_FILL,            "field_of_vision_render.hlsl", "ps_fill")

#define DX11_VERTEX_SHADERS \
	DX11_VS(SPRITE_SHEET_RENDER, "sprite_sheet.hlsl",           "vs_sprite") \
	DX11_VS(SPRITE_SHEET_FONT,   "sprite_sheet.hlsl",           "vs_font") \
	DX11_VS(PASS_THROUGH,        "pass_through_output.hlsl",    "vs_pass_through") \
	DX11_VS(CARD_RENDER,         "card_render.hlsl",            "vs_card") \
	DX11_VS(DDW_TRIANGLE,        "debug_draw_world.hlsl",       "vs_triangle") \
	DX11_VS(DDW_LINE,            "debug_draw_world.hlsl",       "vs_line") \
	DX11_VS(FOV_EDGE,            "field_of_vision_render.hlsl", "vs_edge") \
	DX11_VS(FOV_FILL,            "field_of_vision_render.hlsl", "vs_fill")

#define DX11_COMPUTE_SHADERS \
	DX11_CS(SPRITE_SHEET_CLEAR_SPRITE_ID, "sprite_sheet.hlsl",           "cs_clear_sprite_id") \
	DX11_CS(SPRITE_SHEET_HIGHLIGHT,       "sprite_sheet.hlsl",           "cs_highlight_sprite") \
	DX11_CS(PIXEL_ART_UPSAMPLER,          "pixel_art_upsampler.hlsl",    "cs_pixel_art_upsampler") \
	DX11_CS(PARTICLES,                    "particles.hlsl",              "cs_particles") \
	DX11_CS(FOV_BLEND,                    "field_of_vision_render.hlsl", "cs_shadow_blend") \
	DX11_CS(FOV_COMPOSITE,                "field_of_vision_render.hlsl", "cs_composite")

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

/*
// CB(name, element_size, array_size)
#define CONSTANT_BUFFERS \
	CB_X(TRIANGLE_BUFFER, sizeof(Triangle_Constants), 8) \
	CB_X(SPRITE_BUFFER,   sizeof(Triangle_Constants), 8)

#define CB_1(name, element_size) CB(name, element_size)
#define CB_2(name, element_size) CB(name##_1, element_size) CB(name##_2, element_size)
#define CB_3(name, element_size) CB_2(name, element_size) CB(name##_3, element_size)
#define CB_4(name, element_size) CB_3(name, element_size) CB(name##_4, element_size)
#define CB_5(name, element_size) CB_4(name, element_size) CB(name##_5, element_size)
#define CB_6(name, element_size) CB_5(name, element_size) CB(name##_6, element_size)
#define CB_7(name, element_size) CB_6(name, element_size) CB(name##_7, element_size)
#define CB_8(name, element_size) CB_7(name, element_size) CB(name##_8, element_size)

#define CB_X(name, element_size, array_size) \
	CB_ARRAY(name, element_size, array_size) \
	CB_##array_size(name, element_size)

enum DX11_Constant_Buffer_Array
{
#define CB(...)
#define CB_ARRAY(name, _element_size, _array_size) DX11_CBA_##name,
	CONSTANT_BUFFERS
#undef CB_ARRAY
#undef CB

	NUM_CONSTANT_BUFFER_ARRYS,
};

enum DX11_Constant_Buffer
{
#define CB(name, _element_size, array_size) DX11_CB_##name,
#define CB_ARRAY(...)
	CONSTANT_BUFFERS
#undef CB_ARRAY
#undef CB

	NUM_CONSTANT_BUFFERS,
};
*/

// IB(name, element_width, max_elements, )
#define INSTANCE_BUFFERS \
	IB()

#define MAX_CONSTANT_BUFFERS 64

struct DX11_Renderer
{
	// TODO -- remove this from here?
	HMODULE                    d3d11_library;

	ID3D11Device              *device;
	ID3D11DeviceContext       *device_context;
	IDXGISwapChain            *swap_chain;
	ID3D11Texture2D           *back_buffer;
	ID3D11RenderTargetView    *back_buffer_rtv;

	ID3D11Texture2D           *output_texture;
	ID3D11UnorderedAccessView *output_uav;
	ID3D11RenderTargetView    *output_rtv;
	ID3D11ShaderResourceView  *output_srv;

	ID3D11Buffer              *cbs[MAX_CONSTANT_BUFFERS];

	ID3D11ShaderResourceView  *instance_buffer_srv;
	ID3D11Buffer              *instance_buffer;

	ID3D11RasterizerState     *rasterizer_state;
	ID3D11BlendState          *blend_state;

	ID3D11PixelShader         *ps[NUM_DX11_PIXEL_SHADERS];
	ID3D11VertexShader        *vs[NUM_DX11_VERTEX_SHADERS];
	ID3D11ComputeShader       *cs[NUM_DX11_COMPUTE_SHADERS];

	Max_Length_Array<ID3D11Texture2D*, MAX_TEXTURES> tex;

	Pixel_Art_Upsampler        pixel_art_upsampler;

	v2_u32 max_screen_size;
	v2_u32 screen_size;
};

bool reload_textures(DX11_Renderer* dx11_render, Render* render);

bool init(DX11_Renderer* renderer, Draw* draw, HWND window);
bool set_screen_size(DX11_Renderer* renderer, v2_u32 screen_size);
void free(DX11_Renderer* renderer);
void draw(DX11_Renderer* renderer, Draw* draw, Render* render);