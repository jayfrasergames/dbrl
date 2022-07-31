#ifndef JFG_IMGUI_H
#define JFG_IMGUI_H

#include "prelude.h"
#include "containers.hpp"

#include "imgui_gpu_data_types.h"
// XXX - needed for memset, should remove this later
#include "input.h"

#ifdef JFG_D3D11_H

#ifndef IMGUI_DEFINE_GFX
#define IMGUI_DEFINE_GFX
#endif

struct IMGUI_D3D11_Context
{
	ID3D11Texture2D          *text_texture;
	ID3D11ShaderResourceView *text_texture_srv;
	ID3D11VertexShader       *text_vertex_shader;
	ID3D11PixelShader        *text_pixel_shader;
	ID3D11Buffer             *text_instance_buffer;
	ID3D11ShaderResourceView *text_instance_buffer_srv;
	ID3D11Buffer             *text_constant_buffer;
	ID3D11RasterizerState    *text_rasterizer_state;
};

#endif

#define IMGUI_MAX_TEXT_CHARACTERS 4096
#define IMGUI_MAX_ELEMENTS 4096

struct IMGUI_Element_State
{
	uptr id;
	union {
		struct {
			u8 collapsed;
		} tree_begin;
	};
};

struct IMGUI_Context
{
	v2 text_pos;
	v4 text_color;
	u32 text_index;
	u32 tree_indent_level;
	Input* input;
	v2_u32 screen_size;
	uptr hot_element_id;
	IMGUI_VS_Text_Instance text_buffer[IMGUI_MAX_TEXT_CHARACTERS];

	Max_Length_Array<IMGUI_Element_State, IMGUI_MAX_ELEMENTS> element_states;

#ifdef IMGUI_DEFINE_GFX
	union {
	#ifdef JFG_D3D11_H
		IMGUI_D3D11_Context d3d11;
	#endif
	};
#endif
};

void imgui_begin(IMGUI_Context* context, Input* input, v2_u32 screen_size);
void imgui_set_text_cursor(IMGUI_Context* context, v4 color, v2 pos);
void imgui_text(IMGUI_Context* context, char* format_string, ...);
u8   imgui_tree_begin(IMGUI_Context* context, char* name);
void imgui_tree_end(IMGUI_Context* context);
void imgui_f32(IMGUI_Context* context, char* name, f32* val, f32 min_val, f32 max_val);
void imgui_u32(IMGUI_Context* context, char* name, u32* val, u32 min_val, u32 max_val);
u8   imgui_button(IMGUI_Context* context, char* caption);

#ifndef JFG_HEADER_ONLY
#endif

// d3d11 implementation
#ifdef JFG_D3D11_H

u8 imgui_d3d11_init(IMGUI_Context* context, ID3D11Device* device);
void imgui_d3d11_free(IMGUI_Context* context);
void imgui_d3d11_draw(IMGUI_Context*          imgui,
                      ID3D11DeviceContext*    dc,
                      ID3D11RenderTargetView* output_rtv,
                      v2_u32                  output_view_dimensions);

#ifndef JFG_HEADER_ONLY
#endif
#endif

#endif
