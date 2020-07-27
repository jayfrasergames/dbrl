#ifndef FIELD_OF_VISION_RENDER_H
#define FIELD_OF_VISION_RENDER_H

#include "jfg/prelude.h"
#include "jfg/containers.hpp"

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

#ifndef JFG_HEADER_ONLY
void fov_render_init(Field_Of_Vision_Render* render, v2_u32 grid_size)
{
	render->grid_size = grid_size;
	fov_render_reset(render);
}

void fov_render_reset(Field_Of_Vision_Render* render)
{
	render->fov_draws.reset();
	render->free_buffers.reset();
	for (u32 i = 0; i < FOV_RENDER_MAX_BUFFERS; ++i) {
		render->free_buffers.push(i + 1);
	}
}

u32 fov_render_add_fov(Field_Of_Vision_Render* render, Map_Cache_Bool* fov)
{
	u32 buffer_id = render->free_buffers.pop();
	Field_Of_Vision_Draw draw = {};
	draw.fov = fov;
	draw.id = buffer_id;
	render->fov_draws.append(draw);
	Field_Of_Vision_Entry entry = {};
	entry.id = buffer_id;
	entry.alpha = 0.0f;
	render->fov_entries.append(entry);
	return buffer_id;
}

void fov_render_release_buffer(Field_Of_Vision_Render* render, u32 buffer_id)
{
	render->free_buffers.push(buffer_id);
	for (u32 i = 0; i < render->fov_draws.len; ++i) {
		if (render->fov_draws[i].id == buffer_id) {
			render->fov_draws.remove(i);
			break;
		}
	}
	for (u32 i = 0; i < render->fov_entries.len; ++i) {
		if (render->fov_entries[i].id == buffer_id) {
			render->fov_entries.remove_preserve_order(i);
			return;
		}
	}
	ASSERT(0);
}

void fov_render_set_alpha(Field_Of_Vision_Render* render, u32 buffer_id, f32 alpha)
{
	for (u32 i = 0; i < render->fov_entries.len; ++i) {
		if (render->fov_entries[i].id == buffer_id) {
			render->fov_entries[i].alpha = alpha;
			break;
		}
	}
}
#endif

#ifdef FOV_RENDER_INCLUDE_GFX_API

u8 fov_render_d3d11_init(Field_Of_Vision_Render* render, ID3D11Device* device);
void fov_render_d3d11_free(Field_Of_Vision_Render* render);
void fov_render_d3d11_draw(Field_Of_Vision_Render* render, ID3D11DeviceContext* dc);
void fov_render_d3d11_composite(Field_Of_Vision_Render*    render,
                                ID3D11DeviceContext*       dc,
                                ID3D11UnorderedAccessView* world_uav,
                                v2_u32                     size);

#ifndef JFG_HEADER_ONLY
#include "gen/sprite_sheet_water_edges.data.h"
#include "gen/field_of_vision_edge_vs_dxbc.data.h"
#include "gen/field_of_vision_edge_ps_dxbc.data.h"
#include "gen/field_of_vision_fill_vs_dxbc.data.h"
#include "gen/field_of_vision_fill_ps_dxbc.data.h"
#include "gen/field_of_vision_blend_cs_dxbc.data.h"
#include "gen/field_of_vision_composite_cs_dxbc.data.h"

u8 fov_render_d3d11_init(Field_Of_Vision_Render* render, ID3D11Device* device)
{
	HRESULT hr;

	// init edges texture
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width  = SPRITE_SHEET_WATER_EDGES.size.w;
		desc.Height = SPRITE_SHEET_WATER_EDGES.size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA data_desc = {};
		data_desc.pSysMem = SPRITE_SHEET_WATER_EDGES.image_data;
		data_desc.SysMemPitch = SPRITE_SHEET_WATER_EDGES.size.w * sizeof(u32);
		data_desc.SysMemSlicePitch = 0;

		hr = device->CreateTexture2D(&desc, &data_desc, &render->d3d11.edges);
	}
	if (FAILED(hr)) {
		goto error_init_edges;
	}

	hr = device->CreateShaderResourceView(render->d3d11.edges, NULL, &render->d3d11.edges_srv);
	if (FAILED(hr)) {
		goto error_init_edges_srv;
	}

	u32 inited_buffers = 0;
	for (u32 i = 0; i < FOV_RENDER_MAX_BUFFERS; ++i) {
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = render->grid_size.w * SPRITE_SHEET_WATER_EDGES.sprite_size.w;
		desc.Height = render->grid_size.h * SPRITE_SHEET_WATER_EDGES.sprite_size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		hr = device->CreateTexture2D(&desc, NULL, &render->d3d11.buffers[i].buffer);
		if (FAILED(hr)) {
			goto error_init_buffers;
		}

		hr = device->CreateRenderTargetView(render->d3d11.buffers[i].buffer,
		                                    NULL,
		                                    &render->d3d11.buffers[i].buffer_rtv);
		if (FAILED(hr)) {
			render->d3d11.buffers[i].buffer->Release();
			goto error_init_buffers;
		}

		hr = device->CreateShaderResourceView(render->d3d11.buffers[i].buffer,
		                                      NULL,
		                                      &render->d3d11.buffers[i].buffer_srv);
		if (FAILED(hr)) {
			render->d3d11.buffers[i].buffer_rtv->Release();
			render->d3d11.buffers[i].buffer->Release();
			goto error_init_buffers;
		}

		inited_buffers = i;
	}

	// init fov texture
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = render->grid_size.w * SPRITE_SHEET_WATER_EDGES.sprite_size.w;
		desc.Height = render->grid_size.h * SPRITE_SHEET_WATER_EDGES.sprite_size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		hr = device->CreateTexture2D(&desc, NULL, &render->d3d11.fov);
	}
	if (FAILED(hr)) {
		goto error_init_fov;
	}

	hr = device->CreateShaderResourceView(render->d3d11.fov, NULL, &render->d3d11.fov_srv);
	if (FAILED(hr)) {
		goto error_init_fov_srv;
	}

	hr = device->CreateUnorderedAccessView(render->d3d11.fov, NULL, &render->d3d11.fov_uav);
	if (FAILED(hr)) {
		goto error_init_fov_uav;
	}

	// init constant buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Field_Of_Vision_Render_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Field_Of_Vision_Render_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &render->d3d11.constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_constant_buffer;
	}

	hr = device->CreateVertexShader(FOV_EDGE_DXBC_VS,
	                                ARRAY_SIZE(FOV_EDGE_DXBC_VS),
	                                NULL,
	                                &render->d3d11.edge_vs);
	if (FAILED(hr)) {
		goto error_init_edge_vs;
	}

	hr = device->CreatePixelShader(FOV_EDGE_DXBC_PS,
	                               ARRAY_SIZE(FOV_EDGE_DXBC_PS),
	                               NULL,
	                               &render->d3d11.edge_ps);
	if (FAILED(hr)) {
		goto error_init_edge_ps;
	}

	hr = device->CreateVertexShader(FOV_FILL_DXBC_VS,
	                                ARRAY_SIZE(FOV_FILL_DXBC_VS),
	                                NULL,
	                                &render->d3d11.fill_vs);
	if (FAILED(hr)) {
		goto error_init_fill_vs;
	}

	hr = device->CreatePixelShader(FOV_FILL_DXBC_PS,
	                               ARRAY_SIZE(FOV_FILL_DXBC_PS),
	                               NULL,
	                               &render->d3d11.fill_ps);
	if (FAILED(hr)) {
		goto error_init_fill_ps;
	}

	// create edge instance buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Field_Of_Vision_Edge_Instance) * FOV_RENDER_MAX_EDGE_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Field_Of_Vision_Edge_Instance);

		hr = device->CreateBuffer(&desc, NULL, &render->d3d11.edge_instances);
	}
	if (FAILED(hr)) {
		goto error_init_edge_instance_buffer;
	}

	// create edge instance buffer srv
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements = FOV_RENDER_MAX_EDGE_INSTANCES;

		hr = device->CreateShaderResourceView(render->d3d11.edge_instances,
		                                      &desc,
		                                      &render->d3d11.edge_instances_srv);
	}
	if (FAILED(hr)) {
		goto error_init_edge_instance_buffer_srv;
	}

	// create fill instance buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Field_Of_Vision_Fill_Instance) * FOV_RENDER_MAX_FILL_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Field_Of_Vision_Fill_Instance);

		hr = device->CreateBuffer(&desc, NULL, &render->d3d11.fill_instances);
	}
	if (FAILED(hr)) {
		goto error_init_fill_instance_buffer;
	}

	// create fill instance buffer srv
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements = FOV_RENDER_MAX_FILL_INSTANCES;

		hr = device->CreateShaderResourceView(render->d3d11.fill_instances,
		                                      &desc,
		                                      &render->d3d11.fill_instances_srv);
	}
	if (FAILED(hr)) {
		goto error_init_fill_instance_buffer_srv;
	}

	// create rasterizer state
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.DepthClipEnable = FALSE;
		desc.ScissorEnable = FALSE;
		desc.MultisampleEnable = FALSE;
		desc.AntialiasedLineEnable = FALSE;

		hr = device->CreateRasterizerState(&desc, &render->d3d11.rasterizer_state);
	}
	if (FAILED(hr)) {
		goto error_init_rasterizer_state;
	}

	hr = device->CreateComputeShader(FOV_BLEND_DXBC_CS,
	                                 ARRAY_SIZE(FOV_BLEND_DXBC_CS),
	                                 NULL,
	                                 &render->d3d11.blend_cs);
	if (FAILED(hr)) {
		goto error_init_blend_cs;
	}

	// init blend constant buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Field_Of_Vision_Render_Blend_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Field_Of_Vision_Render_Blend_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &render->d3d11.blend_constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_blend_constant_buffer;
	}


	hr = device->CreateComputeShader(FOV_COMPOSITE_DXBC_CS,
	                                 ARRAY_SIZE(FOV_COMPOSITE_DXBC_CS),
	                                 NULL,
	                                 &render->d3d11.composite_cs);
	if (FAILED(hr)) {
		goto error_init_composite_cs;
	}

	return 1;

	render->d3d11.composite_cs->Release();
error_init_composite_cs:
	render->d3d11.blend_constant_buffer->Release();
error_init_blend_constant_buffer:
	render->d3d11.blend_cs->Release();
error_init_blend_cs:
	render->d3d11.rasterizer_state->Release();
error_init_rasterizer_state:
	render->d3d11.fill_instances_srv->Release();
error_init_fill_instance_buffer_srv:
	render->d3d11.fill_instances->Release();
error_init_fill_instance_buffer:
	render->d3d11.edge_instances_srv->Release();
error_init_edge_instance_buffer_srv:
	render->d3d11.edge_instances->Release();
error_init_edge_instance_buffer:
	render->d3d11.fill_ps->Release();
error_init_fill_ps:
	render->d3d11.fill_vs->Release();
error_init_fill_vs:
	render->d3d11.edge_ps->Release();
error_init_edge_ps:
	render->d3d11.edge_vs->Release();
error_init_edge_vs:
	render->d3d11.constant_buffer->Release();
error_init_constant_buffer:
	render->d3d11.fov_uav->Release();
error_init_fov_uav:
	render->d3d11.fov_srv->Release();
error_init_fov_srv:
	render->d3d11.fov->Release();
error_init_fov:
error_init_buffers:
	for (u32 i = 0; i < inited_buffers; ++i) {
		render->d3d11.buffers[i].buffer_srv->Release();
		render->d3d11.buffers[i].buffer_rtv->Release();
		render->d3d11.buffers[i].buffer->Release();
	}
	render->d3d11.edges_srv->Release();
error_init_edges_srv:
	render->d3d11.edges->Release();
error_init_edges:
	return 0;
}

void fov_render_d3d11_free(Field_Of_Vision_Render* render)
{
	render->d3d11.composite_cs->Release();
	render->d3d11.blend_constant_buffer->Release();
	render->d3d11.blend_cs->Release();
	render->d3d11.rasterizer_state->Release();
	render->d3d11.fill_instances_srv->Release();
	render->d3d11.fill_instances->Release();
	render->d3d11.edge_instances_srv->Release();
	render->d3d11.edge_instances->Release();
	render->d3d11.fill_ps->Release();
	render->d3d11.fill_vs->Release();
	render->d3d11.edge_ps->Release();
	render->d3d11.edge_vs->Release();
	render->d3d11.constant_buffer->Release();
	render->d3d11.fov_uav->Release();
	render->d3d11.fov_srv->Release();
	render->d3d11.fov->Release();
	for (u32 i = 0; i < FOV_RENDER_MAX_BUFFERS; ++i) {
		render->d3d11.buffers[i].buffer_srv->Release();
		render->d3d11.buffers[i].buffer_rtv->Release();
		render->d3d11.buffers[i].buffer->Release();
	}
	render->d3d11.edges_srv->Release();
	render->d3d11.edges->Release();
}

void fov_render_d3d11_draw(Field_Of_Vision_Render* render, ID3D11DeviceContext* dc)
{
	dc->ClearState();

	// set viewport
	{
		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width  = (f32)(render->grid_size.w * SPRITE_SHEET_WATER_EDGES.sprite_size.w);
		viewport.Height = (f32)(render->grid_size.h * SPRITE_SHEET_WATER_EDGES.sprite_size.h);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		dc->RSSetViewports(1, &viewport);
	}

	dc->RSSetState(render->d3d11.rasterizer_state);

	dc->IASetInputLayout(NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};
	hr = dc->Map(render->d3d11.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	Field_Of_Vision_Render_Constant_Buffer constant_buffer = {};
	constant_buffer.grid_size = V2_f32(256.0f, 256.0f);
	constant_buffer.sprite_size = V2_f32(24.0f, 24.0f);
	constant_buffer.edge_tex_size = (v2)SPRITE_SHEET_WATER_EDGES.size;
	memcpy(mapped_buffer.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(render->d3d11.constant_buffer, 0);

	dc->VSSetConstantBuffers(0, 1, &render->d3d11.constant_buffer);
	dc->PSSetConstantBuffers(0, 1, &render->d3d11.constant_buffer);

	// draw any fov buffers required
	auto& fov_draws = render->fov_draws;
	for (u32 i = 0; i < fov_draws.len; ++i) {
		Max_Length_Array<Field_Of_Vision_Fill_Instance,
		                 FOV_RENDER_MAX_FILL_INSTANCES> fill_instances;
		Max_Length_Array<Field_Of_Vision_Edge_Instance,
		                 FOV_RENDER_MAX_EDGE_INSTANCES> edge_instances;
		fill_instances.reset();
		edge_instances.reset();
		Field_Of_Vision_Draw draw = fov_draws[i];
		u32 buffer_idx = draw.id - 1;

		Map_Cache_Bool *fov = draw.fov;
		for (u32 y = 1; y < 255; ++y) {
			for (u32 x = 1; x < 255; ++x) {
				bool tl = fov->get(V2_u8(x - 1, y - 1));
				bool t  = fov->get(V2_u8(    x, y - 1));
				bool tr = fov->get(V2_u8(x + 1, y - 1));
				bool l  = fov->get(V2_u8(x - 1,     y));
				bool c  = fov->get(V2_u8(    x,     y));
				bool r  = fov->get(V2_u8(x + 1,     y));
				bool bl = fov->get(V2_u8(x - 1, y + 1));
				bool b  = fov->get(V2_u8(    x, y + 1));
				bool br = fov->get(V2_u8(x + 1, y + 1));
				u8 mask = 0;
				if (t)  { mask |= 0x01; }
				if (tr) { mask |= 0x02; }
				if (r)  { mask |= 0x04; }
				if (br) { mask |= 0x08; }
				if (b)  { mask |= 0x10; }
				if (bl) { mask |= 0x20; }
				if (l)  { mask |= 0x40; }
				if (tl) { mask |= 0x80; }
				if (c && mask == 0xFF) {
					Field_Of_Vision_Fill_Instance instance = {};
					instance.world_coords = V2_f32((f32)x, (f32)y);
					fill_instances.append(instance);
				} else if (c) {
					Field_Of_Vision_Edge_Instance instance = {};
					instance.world_coords = V2_f32((f32)x, (f32)y);
					instance.sprite_coords = V2_f32((f32)(mask % 16),
					                                (f32)(15 - mask / 16));
					edge_instances.append(instance);
				}
			}
		}

		ID3D11RenderTargetView *rtv = render->d3d11.buffers[buffer_idx].buffer_rtv;
		float clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
		dc->ClearRenderTargetView(rtv, clear_color);
		dc->OMSetRenderTargets(1, &rtv, NULL);

		hr = dc->Map(render->d3d11.fill_instances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
		ASSERT(SUCCEEDED(hr));
		memcpy(mapped_buffer.pData,
		       &fill_instances.items,
		       sizeof(fill_instances.items[0]) * fill_instances.len);
		dc->Unmap(render->d3d11.fill_instances, 0);

		dc->VSSetShader(render->d3d11.fill_vs, NULL, 0);
		dc->VSSetShaderResources(0, 1, &render->d3d11.fill_instances_srv);
		dc->PSSetShader(render->d3d11.fill_ps, NULL, 0);
		dc->DrawInstanced(6, fill_instances.len, 0, 0);

		hr = dc->Map(render->d3d11.edge_instances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
		ASSERT(SUCCEEDED(hr));
		memcpy(mapped_buffer.pData,
		       &edge_instances.items,
		       sizeof(edge_instances.items[0]) * edge_instances.len);
		dc->Unmap(render->d3d11.edge_instances, 0);

		dc->VSSetShader(render->d3d11.edge_vs, NULL, 0);
		dc->VSSetShaderResources(0, 1, &render->d3d11.edge_instances_srv);
		dc->PSSetShader(render->d3d11.edge_ps, NULL, 0);
		dc->PSSetShaderResources(0, 1, &render->d3d11.edges_srv);
		dc->DrawInstanced(6, edge_instances.len, 0, 0);
	}
	fov_draws.reset();

	dc->ClearState();

	float clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearUnorderedAccessViewFloat(render->d3d11.fov_uav, clear_color);
	auto& fov_entries = render->fov_entries;

	// XXX - clean up unused fov buffers
	{
		u32 clean_to = 0;
		for (u32 i = 0; i < fov_entries.len; ++i) {
			if (fov_entries[i].alpha == 1.0f) {
				clean_to = i;
			}
		}
		for (u32 i = 0; i < clean_to; ++i) {
			fov_render_release_buffer(render, fov_entries[i].id);
		}
	}

	dc->CSSetShader(render->d3d11.blend_cs, NULL, 0);
	dc->CSSetUnorderedAccessViews(0, 1, &render->d3d11.fov_uav, NULL);
	dc->CSSetConstantBuffers(0, 1, &render->d3d11.blend_constant_buffer);
	for (u32 i = 0; i < fov_entries.len; ++i) {
		Field_Of_Vision_Render_Blend_Constant_Buffer cb = {};
		cb.alpha = fov_entries[i].alpha;
		u32 buffer_idx = fov_entries[i].id - 1;

		hr = dc->Map(render->d3d11.blend_constant_buffer,
		             0,
		             D3D11_MAP_WRITE_DISCARD,
		             0,
		             &mapped_buffer);
		ASSERT(SUCCEEDED(hr));
		memcpy(mapped_buffer.pData, &cb, sizeof(cb));
		dc->Unmap(render->d3d11.blend_constant_buffer, 0);

		dc->CSSetShaderResources(0, 1, &render->d3d11.buffers[buffer_idx].buffer_srv);
		v2_u32 pix_size = render->grid_size * SPRITE_SHEET_WATER_EDGES.sprite_size;
		v2_u32 cs_size = V2_u32(FOV_SHADOW_BLEND_CS_WIDTH, FOV_SHADOW_BLEND_CS_HEIGHT);
		v2_u32 dispatch_size = (pix_size + cs_size - (u32)1) / cs_size;
		dc->Dispatch(dispatch_size.w, dispatch_size.h, 1);
	}
}

void fov_render_d3d11_composite(Field_Of_Vision_Render*    render,
                                ID3D11DeviceContext*       dc,
                                ID3D11UnorderedAccessView* world_uav,
                                v2_u32                     size)
{
	dc->ClearState();

	dc->CSSetShader(render->d3d11.composite_cs, NULL, 0);
	dc->CSSetShaderResources(0, 1, &render->d3d11.fov_srv);
	dc->CSSetUnorderedAccessViews(0, 1, &world_uav, NULL);
	dc->Dispatch((size.w + FOV_COMPOSITE_WIDTH  - 1) / FOV_COMPOSITE_WIDTH,
	             (size.h + FOV_COMPOSITE_HEIGHT - 1) / FOV_COMPOSITE_HEIGHT,
	             1);
}
#endif // JFG_HEADER_ONLY
#endif // FOV_RENDER_INCLUDE_GFX_API

#endif
