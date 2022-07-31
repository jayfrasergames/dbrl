#ifndef FIELD_OF_VISION_RENDER_H
#define FIELD_OF_VISION_RENDER_H

#include "prelude.h"
#include "containers.hpp"
#include "types.h"

#include "field_of_vision_render_gpu_data_types.h"

// XXX - need these definitions here

#define FOV_RENDER_MAX_BUFFERS 8
#define FOV_RENDER_MAX_EDGE_INSTANCES (256 * 256)
#define FOV_RENDER_MAX_FILL_INSTANCES (256 * 256)

#ifdef JFG_D3D11_H

#ifndef FOV_RENDER_INCLUDE_GFX_API
#define FOV_RENDER_INCLUDE_GFX_API
#endif

struct Field_Of_Vision_Render_D3D11
{
	ID3D11Texture2D          *edges;
	ID3D11ShaderResourceView *edges_srv;
	struct {
		ID3D11Texture2D          *buffer;
		ID3D11ShaderResourceView *buffer_srv;
		ID3D11RenderTargetView   *buffer_rtv;
	} buffers[FOV_RENDER_MAX_BUFFERS];
	ID3D11Texture2D           *fov;
	ID3D11ShaderResourceView  *fov_srv;
	ID3D11UnorderedAccessView *fov_uav;
	ID3D11Buffer              *constant_buffer;
	ID3D11Buffer              *edge_instances;
	ID3D11ShaderResourceView  *edge_instances_srv;
	ID3D11VertexShader        *edge_vs;
	ID3D11PixelShader         *edge_ps;
	ID3D11Buffer              *fill_instances;
	ID3D11ShaderResourceView  *fill_instances_srv;
	ID3D11VertexShader        *fill_vs;
	ID3D11PixelShader         *fill_ps;
	ID3D11RasterizerState     *rasterizer_state;
	ID3D11ComputeShader       *blend_cs;
	ID3D11Buffer              *blend_constant_buffer;
	ID3D11ComputeShader       *composite_cs;
};
#endif

struct Field_Of_Vision_Draw
{
	Map_Cache_Bool *fov;
	u32             id;
};

struct Field_Of_Vision_Entry
{
	u32 id;
	f32 alpha;
};

struct Field_Of_Vision_Render
{
	v2_u32 grid_size;
	Max_Length_Array<Field_Of_Vision_Draw, FOV_RENDER_MAX_BUFFERS> fov_draws;
	Stack<u32, FOV_RENDER_MAX_BUFFERS> free_buffers;
	Max_Length_Array<Field_Of_Vision_Entry, FOV_RENDER_MAX_BUFFERS> fov_entries;

#ifdef FOV_RENDER_INCLUDE_GFX_API
	union {
	#ifdef JFG_D3D11_H
		Field_Of_Vision_Render_D3D11 d3d11;
	#endif
	};
#endif
};

void fov_render_init(Field_Of_Vision_Render* render, v2_u32 grid_size);
void fov_render_reset(Field_Of_Vision_Render* render);
u32 fov_render_add_fov(Field_Of_Vision_Render* render, Map_Cache_Bool* fov);
void fov_render_release_buffer(Field_Of_Vision_Render* render, u32 buffer_id);
void fov_render_set_alpha(Field_Of_Vision_Render* render, u32 buffer_id, f32 alpha);

#ifdef FOV_RENDER_INCLUDE_GFX_API

u8 fov_render_d3d11_init(Field_Of_Vision_Render* render, ID3D11Device* device);
void fov_render_d3d11_free(Field_Of_Vision_Render* render);
void fov_render_d3d11_draw(Field_Of_Vision_Render* render, ID3D11DeviceContext* dc);
void fov_render_d3d11_composite(Field_Of_Vision_Render*    render,
                                ID3D11DeviceContext*       dc,
                                ID3D11UnorderedAccessView* world_uav,
                                v2_u32                     size);

#ifndef JFG_HEADER_ONLY
#endif // JFG_HEADER_ONLY
#endif // FOV_RENDER_INCLUDE_GFX_API

#endif
