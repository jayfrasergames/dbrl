#ifndef SPRITE_SHEET_H
#define SPRITE_SHEET_H

#include "prelude.h"
#include "containers.hpp"

#include "particles.h"

// =============================================================================
// GFX API data defintions

#ifdef JFG_D3D11_H

#ifndef SRPITE_SHEET_DEFINE_GFX
#define SPRITE_SHEET_DEFINE_GFX
#endif

struct Sprite_Sheet_D3D11_Instances
{
	ID3D11Buffer             *constant_buffer;
	ID3D11Buffer             *instance_buffer;
	ID3D11ShaderResourceView *instance_buffer_srv;
	ID3D11Texture2D          *texture;
	ID3D11ShaderResourceView *texture_srv;
};

struct Sprite_Sheet_D3D11_Font_Instances
{
	ID3D11Buffer             *constant_buffer;
	ID3D11Buffer             *instance_buffer;
	ID3D11ShaderResourceView *instance_buffer_srv;
	ID3D11Texture2D          *texture;
	ID3D11ShaderResourceView *texture_srv;
};

struct Sprite_Sheet_D3D11_Renderer
{
	ID3D11Texture2D           *output;
	ID3D11RenderTargetView    *output_rtv;
	ID3D11UnorderedAccessView *output_uav;
	ID3D11ShaderResourceView  *output_srv;
	ID3D11Texture2D           *depth_buffer;
	ID3D11DepthStencilView    *depth_buffer_dsv;
	ID3D11DepthStencilState   *depth_stencil_state;
	ID3D11Texture2D           *sprite_id_buffer;
	ID3D11RenderTargetView    *sprite_id_buffer_rtv;
	ID3D11UnorderedAccessView *sprite_id_buffer_uav;
	ID3D11ShaderResourceView  *sprite_id_buffer_srv;
	ID3D11ComputeShader       *clear_sprite_id_compute_shader;
	ID3D11ComputeShader       *highlight_sprite_compute_shader;
	ID3D11Buffer              *highlight_constant_buffer;
	ID3D11VertexShader        *vertex_shader;
	ID3D11PixelShader         *pixel_shader;
	ID3D11RasterizerState     *rasterizer_state;
	ID3D11VertexShader        *font_vertex_shader;
	ID3D11PixelShader         *font_pixel_shader;
	ID3D11RasterizerState     *font_rasterizer_state;
};
#endif

// =============================================================================
// Sprite_Sheet data defintion

#include "sprite_sheet_gpu_data_types.h"

struct Sprite_Sheet_Data
{
	v2_u32 size;
	v2_u32 sprite_size;
	u32 *image_data;
	u8  *mouse_map_data;
};

#define SPRITE_SHEET_MAX_INSTANCES 10240
struct Sprite_Sheet_Instances
{
	Sprite_Sheet_Data data;

	u32 num_instances;
	Sprite_Sheet_Instance instances[SPRITE_SHEET_MAX_INSTANCES];

#ifdef SPRITE_SHEET_DEFINE_GFX
	union {
	#ifdef JFG_D3D11_H
		Sprite_Sheet_D3D11_Instances d3d11;
	#endif
	};
#endif
};

struct Sprite_Sheet_Font_Instances
{
	v2_u32  tex_size;
	void   *tex_data;

	Max_Length_Array<Sprite_Sheet_Font_Instance, SPRITE_SHEET_MAX_INSTANCES> instances;

#ifdef SPRITE_SHEET_DEFINE_GFX
	union {
	#ifdef JFG_D3D11_H
		Sprite_Sheet_D3D11_Font_Instances d3d11;
	#endif
	};
#endif
};

struct Sprite_Sheet_Renderer
{
	v2_u32 size;
	u32                                highlighted_sprite;
	u32                                num_instance_buffers;
	Sprite_Sheet_Instances*            instance_buffers;
	Slice<Sprite_Sheet_Font_Instances> font_instance_buffers;
	Particles                          particles;
	f32                                time;

#ifdef SPRITE_SHEET_DEFINE_GFX
	union {
	#ifdef JFG_D3D11_H
		Sprite_Sheet_D3D11_Renderer d3d11;
	#endif
	};
#endif
};

void sprite_sheet_renderer_init(Sprite_Sheet_Renderer* renderer,
                                Sprite_Sheet_Instances* instance_buffers,
                                u32 num_instance_buffers,
                                v2_u32 size);
u32 sprite_sheet_renderer_id_in_pos(Sprite_Sheet_Renderer* renderer, v2_u32 pos);
void sprite_sheet_renderer_highlight_sprite(Sprite_Sheet_Renderer* renderer, u32 sprite_id);

void sprite_sheet_instances_reset(Sprite_Sheet_Instances* instances);
void sprite_sheet_instances_add(Sprite_Sheet_Instances* instances, Sprite_Sheet_Instance instance);

void sprite_sheet_font_instances_reset(Sprite_Sheet_Font_Instances* instances);
void sprite_sheet_font_instances_add(Sprite_Sheet_Font_Instances* instances,
                                     Sprite_Sheet_Font_Instance   instance);

#ifndef JFG_HEADER_ONLY
#endif

// =============================================================================
// GFX API function definitions

#ifdef JFG_D3D11_H

u8 sprite_sheet_renderer_d3d11_init(Sprite_Sheet_Renderer* renderer,
                                    ID3D11Device*          device);
void sprite_sheet_renderer_d3d11_free(Sprite_Sheet_Renderer* renderer);
void sprite_sheet_renderer_d3d11_highlight_sprite(Sprite_Sheet_Renderer* renderer,
                                                  ID3D11DeviceContext*   dc);

u8 sprite_sheet_instances_d3d11_init(Sprite_Sheet_Instances* instances, ID3D11Device* device);
void sprite_sheet_instances_d3d11_free(Sprite_Sheet_Instances* instances);

u8 sprite_sheet_font_instances_d3d11_init(Sprite_Sheet_Font_Instances* instances, ID3D11Device* device);
void sprite_sheet_font_instances_d3d11_free(Sprite_Sheet_Font_Instances* instances);

void sprite_sheet_renderer_d3d11_begin(Sprite_Sheet_Renderer*  renderer,
                                       ID3D11DeviceContext*    dc);
void sprite_sheet_instances_d3d11_draw(Sprite_Sheet_Renderer* renderer,
                                       Sprite_Sheet_Instances* instances,
                                       ID3D11DeviceContext*    dc);
void sprite_sheet_renderer_d3d11_begin_font(Sprite_Sheet_Renderer* renderer,
                                            ID3D11DeviceContext*   dc);
void sprite_sheet_font_instances_d3d11_draw(Sprite_Sheet_Renderer* renderer,
                                            Sprite_Sheet_Font_Instances* instances,
                                            ID3D11DeviceContext*         dc);
void sprite_sheet_renderer_d3d11_do_particles(Sprite_Sheet_Renderer* renderer,
                                              ID3D11DeviceContext* dc);
void sprite_sheet_renderer_d3d11_end(Sprite_Sheet_Renderer*  renderer,
                                     ID3D11DeviceContext*    dc);
#endif // JFG_D3D11_H

#endif
