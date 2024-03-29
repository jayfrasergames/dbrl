#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "jfg_error.h"

#include "draw.h"
#include "jfg_d3d11.h"
#include <d3d11_1.h>
#include <dxgi1_2.h>

#include "log.h"
#include "render.h"

// SHADER(name, filename, shader_model, entry_point)
#define DX11_PIXEL_SHADERS \
	DX11_PS(SPRITE,              "sprites.hlsl",                "ps_sprite") \
	DX11_PS(TRIANGLE,            "triangle.hlsl",               "ps_triangle") \
	DX11_PS(SPRITE_SHEET_RENDER, "sprite_sheet.hlsl",           "ps_sprite") \
	DX11_PS(SPRITE_SHEET_FONT,   "sprite_sheet.hlsl",           "ps_font") \
	DX11_PS(PASS_THROUGH,        "pass_through_output.hlsl",    "ps_pass_through") \
	DX11_PS(CARD_RENDER,         "card_render.hlsl",            "ps_card") \
	DX11_PS(DDW_TRIANGLE,        "debug_draw_world.hlsl",       "ps_triangle") \
	DX11_PS(DDW_LINE,            "debug_draw_world.hlsl",       "ps_line") \
	DX11_PS(FOV_EDGE,            "field_of_vision_render.hlsl", "ps_edge") \
	DX11_PS(FOV_FILL,            "field_of_vision_render.hlsl", "ps_fill") \
	DX11_PS(PARTICLES,           "particles.hlsl",              "ps_particles")

#define DX11_VERTEX_SHADERS \
	DX11_VS(TRIANGLE,            "triangle.hlsl",               "vs_triangle") \
	DX11_VS(SPRITE,              "sprites.hlsl",                "vs_sprite") \
	DX11_VS(SPRITE_SHEET_RENDER, "sprite_sheet.hlsl",           "vs_sprite") \
	DX11_VS(SPRITE_SHEET_FONT,   "sprite_sheet.hlsl",           "vs_font") \
	DX11_VS(PASS_THROUGH,        "pass_through_output.hlsl",    "vs_pass_through") \
	DX11_VS(CARD_RENDER,         "card_render.hlsl",            "vs_card") \
	DX11_VS(DDW_TRIANGLE,        "debug_draw_world.hlsl",       "vs_triangle") \
	DX11_VS(DDW_LINE,            "debug_draw_world.hlsl",       "vs_line") \
	DX11_VS(FOV_EDGE,            "field_of_vision_render.hlsl", "vs_edge") \
	DX11_VS(FOV_FILL,            "field_of_vision_render.hlsl", "vs_fill") \
	DX11_VS(PARTICLES,           "particles.hlsl",              "vs_particles")

#define DX11_COMPUTE_SHADERS \
	DX11_CS(SPRITE_SHEET_CLEAR_SPRITE_ID, "sprite_sheet.hlsl",           "cs_clear_sprite_id") \
	DX11_CS(SPRITE_SHEET_HIGHLIGHT,       "sprite_sheet.hlsl",           "cs_highlight_sprite") \
	DX11_CS(PIXEL_ART_UPSAMPLER,          "pixel_art_upsampler.hlsl",    "cs_pixel_art_upsampler") \
	DX11_CS(FOV_BLEND,                    "field_of_vision_render.hlsl", "cs_shadow_blend") \
	DX11_CS(FOV_COMPOSITE,                "field_of_vision_render.hlsl", "cs_composite") \
	DX11_CS(FOV_CLEAR,                    "field_of_vision_render.hlsl", "cs_clear")

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

#define MAX_CONSTANT_BUFFERS 64

struct DX11_Renderer
{
	Log                       *log;
	// TODO -- remove this from here?
	HMODULE                    d3d11_library;

	ID3D11Device              *device;
	ID3D11DeviceContext       *device_context;
	ID3D11InfoQueue           *info_queue;

	ID3DUserDefinedAnnotation *user_defined_annotation;

	IDXGISwapChain            *swap_chain;
	ID3D11Texture2D           *back_buffer;
	ID3D11RenderTargetView    *back_buffer_rtv;

	ID3D11Texture2D           *output_texture;
	ID3D11UnorderedAccessView *output_uav;
	ID3D11RenderTargetView    *output_rtv;
	ID3D11ShaderResourceView  *output_srv;

	ID3D11Buffer              *cbs[MAX_CONSTANT_BUFFERS];
	ID3D11Buffer              *dispatch_cbs[MAX_CONSTANT_BUFFERS];

	ID3D11Texture2D           *target_textures[NUM_TARGET_TEXTURES];
	ID3D11ShaderResourceView  *target_texture_srvs[NUM_TARGET_TEXTURES];
	ID3D11UnorderedAccessView *target_texture_uavs[NUM_TARGET_TEXTURES];
	ID3D11RenderTargetView    *target_texture_rtvs[NUM_TARGET_TEXTURES];
	ID3D11DepthStencilView    *target_texture_dsvs[NUM_TARGET_TEXTURES];

	ID3D11DepthStencilState   *depth_stencil_state; 

	ID3D11InputLayout         *input_layouts[NUM_INSTANCE_BUFFERS];
	ID3D11Buffer              *instance_buffers[NUM_INSTANCE_BUFFERS];

	ID3D11RasterizerState     *rasterizer_state;
	ID3D11BlendState          *blend_state;

	ID3D11PixelShader         *ps[NUM_DX11_PIXEL_SHADERS];
	ID3D11VertexShader        *vs[NUM_DX11_VERTEX_SHADERS];
	ID3D11ComputeShader       *cs[NUM_DX11_COMPUTE_SHADERS];

	ID3DBlob                  *vs_code[NUM_DX11_VERTEX_SHADERS];

	ID3D11ShaderResourceView  *srvs[NUM_SOURCE_TEXTURES];
	ID3D11Texture2D           *tex[NUM_SOURCE_TEXTURES];

	v2_u32 max_screen_size;
	v2_u32 screen_size;
};

bool reload_textures(DX11_Renderer* dx11_render, Render* render);

JFG_Error init(DX11_Renderer* renderer, Render* render, Log* log, Draw* draw, HWND window);
bool set_screen_size(DX11_Renderer* renderer, v2_u32 screen_size);
bool check_errors(DX11_Renderer* renderer, u32 frame_number);
void free(DX11_Renderer* renderer);

void begin_frame(DX11_Renderer* renderer, Render* render);
void draw(DX11_Renderer* renderer, Draw* draw, Render* render);