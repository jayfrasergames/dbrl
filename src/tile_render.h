#ifndef TILE_RENDER_H
#define TILE_RENDER_H

#include "jfg/prelude.h"
#include "tile_render_gpu_data_types.h"

struct Tile_Render_Texture
{
	v2_u32 dimensions;
	v2_u32 tile_size;
	void *data;
};

#define TILE_RENDER_MAX_INSTANCES 4096

struct Tile_Render
{
	Tile_Render_Texture texture;
	u32 num_instances;
	Tile_Render_Tile_Instance tile_instances[TILE_RENDER_MAX_INSTANCES];
};

void tile_render_init(Tile_Render* context, Tile_Render_Texture* texture);

#ifndef JFG_HEADER_ONLY

// XXX - for memset, should probably get rid of this later
#include <string.h>

void tile_render_init(Tile_Render* tile_render, Tile_Render_Texture* texture)
{
	memset(tile_render, 0, sizeof(tile_render));
	tile_render->texture = *texture;
	tile_render->num_instances = 1;
	tile_render->tile_instances[0].sprite_pos = { 10.0f, 10.0f };
	tile_render->tile_instances[0].world_pos = { 1.0f, 1.0f };
}
#endif

// d3d11
#ifdef JFG_D3D11_H

struct Tile_Render_D3D11_Context
{
	ID3D11Texture2D          *tile_tex;
	ID3D11ShaderResourceView *tile_tex_srv;
	ID3D11VertexShader       *tile_vertex_shader;
	ID3D11PixelShader        *tile_pixel_shader;
	ID3D11Buffer             *tile_instance_buffer;
	ID3D11ShaderResourceView *tile_instance_buffer_srv;
	ID3D11Buffer             *tile_constant_buffer;
	ID3D11RasterizerState    *tile_rasterizer_state;
};

u8 tile_render_d3d11_init(Tile_Render_D3D11_Context* context,
                          Tile_Render*               tile_render,
                          ID3D11Device*              device);

void tile_render_d3d11_draw(Tile_Render_D3D11_Context* tile_render_d3d11,
                            Tile_Render*               tile_render,
                            ID3D11DeviceContext*       d3d11,
                            ID3D11RenderTargetView*    output_rtv,
                            v2                         screen_size);

#ifndef JFG_HEADER_ONLY
#include "gen/tile_render_dxbc_tile_vertex_shader.data.h"
#include "gen/tile_render_dxbc_tile_pixel_shader.data.h"

u8 tile_render_d3d11_init(Tile_Render_D3D11_Context* context,
                          Tile_Render*               tile_render,
                          ID3D11Device*              device)
{
	Tile_Render_Texture header = tile_render->texture;
	HRESULT hr;
	ID3D11Texture2D *tile_tex;
	{
		D3D11_TEXTURE2D_DESC tex_desc = {};
		tex_desc.Width = header.dimensions.w;
		tex_desc.Height = header.dimensions.h;
		tex_desc.MipLevels = 1;
		tex_desc.ArraySize = 1;
		tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		tex_desc.SampleDesc.Count = 1;
		tex_desc.SampleDesc.Quality = 0;
		tex_desc.Usage = D3D11_USAGE_IMMUTABLE;
		tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		tex_desc.CPUAccessFlags = 0;
		tex_desc.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA data_desc = {};
		data_desc.pSysMem = header.data;
		data_desc.SysMemPitch = header.dimensions.w * sizeof(u32);
		data_desc.SysMemSlicePitch = 0;

		hr = device->CreateTexture2D(&tex_desc, &data_desc, &tile_tex);
	}
	if (FAILED(hr)) {
		goto error_init_tile_tex;
	}

	ID3D11ShaderResourceView *tile_tex_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(tile_tex, &srv_desc, &tile_tex_srv);
	}
	if (FAILED(hr)) {
		goto error_init_tile_tex_srv;
	}

	ID3D11VertexShader *tile_vertex_shader;
	hr = device->CreateVertexShader(TILE_RENDER_TILE_VS, ARRAY_SIZE(TILE_RENDER_TILE_VS), NULL,
		&tile_vertex_shader);
	if (FAILED(hr)) {
		goto error_init_tile_vertex_shader;
	}

	ID3D11PixelShader *tile_pixel_shader;
	hr = device->CreatePixelShader(TILE_RENDER_TILE_PS, ARRAY_SIZE(TILE_RENDER_TILE_PS), NULL,
		&tile_pixel_shader);
	if (FAILED(hr)) {
		goto error_init_tile_pixel_shader;
	}

	ID3D11Buffer *tile_instance_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Tile_Render_Tile_Instance) * TILE_RENDER_MAX_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Tile_Render_Tile_Instance);

		hr = device->CreateBuffer(&desc, NULL, &tile_instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_tile_instance_buffer;
	}

	ID3D11ShaderResourceView *tile_instance_buffer_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_UNKNOWN;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv_desc.Buffer.ElementOffset = 0;
		srv_desc.Buffer.ElementWidth = TILE_RENDER_MAX_INSTANCES;

		hr = device->CreateShaderResourceView(tile_instance_buffer,
			&srv_desc, &tile_instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_tile_instance_buffer_srv;
	}

	ID3D11Buffer *tile_constant_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Tile_Render_CB_Tile);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Tile_Render_CB_Tile);

		hr = device->CreateBuffer(&desc, NULL, &tile_constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_tile_constant_buffer;
	}

	ID3D11RasterizerState *tile_rasterizer_state;
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;

		hr = device->CreateRasterizerState(&desc, &tile_rasterizer_state);
	}
	if (FAILED(hr)) {
		goto error_init_tile_rasterizer_state;
	}

	context->tile_tex                 = tile_tex;
	context->tile_tex_srv             = tile_tex_srv;
	context->tile_vertex_shader       = tile_vertex_shader;
	context->tile_pixel_shader        = tile_pixel_shader;
	context->tile_instance_buffer     = tile_instance_buffer;
	context->tile_instance_buffer_srv = tile_instance_buffer_srv;
	context->tile_constant_buffer     = tile_constant_buffer;
	context->tile_rasterizer_state    = tile_rasterizer_state;

	return 1;

	tile_rasterizer_state->Release();
error_init_tile_rasterizer_state:
	tile_constant_buffer->Release();
error_init_tile_constant_buffer:
	tile_instance_buffer_srv->Release();
error_init_tile_instance_buffer_srv:
	tile_instance_buffer->Release();
error_init_tile_instance_buffer:
	tile_pixel_shader->Release();
error_init_tile_pixel_shader:
	tile_vertex_shader->Release();
error_init_tile_vertex_shader:
	tile_tex_srv->Release();
error_init_tile_tex_srv:
	tile_tex->Release();
error_init_tile_tex:
	return 0;
}

void tile_render_d3d11_draw(Tile_Render_D3D11_Context* tile_render_d3d11,
                            Tile_Render*               tile_render,
                            ID3D11DeviceContext*       d3d11,
                            ID3D11RenderTargetView*    output_rtv,
                            v2                         screen_size)
{
	Tile_Render_CB_Tile tile_constant_buffer = {};
	tile_constant_buffer.screen_size = screen_size;
	// tile_constant_buffer.screen_size.h = screen_size.h
	tile_constant_buffer.tile_size.w = tile_render->texture.tile_size.w;
	tile_constant_buffer.tile_size.h = tile_render->texture.tile_size.h;
	tile_constant_buffer.tex_size.w = tile_render->texture.dimensions.w;
	tile_constant_buffer.tex_size.h = tile_render->texture.dimensions.h;
	tile_constant_buffer.zoom = 4.0f;

	D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};

	HRESULT hr;
	hr = d3d11->Map(tile_render_d3d11->tile_instance_buffer,
		0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	assert(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &tile_render->tile_instances,
		sizeof(IMGUI_VS_Text_Instance) * tile_render->num_instances);
	d3d11->Unmap(tile_render_d3d11->tile_instance_buffer, 0);

	hr = d3d11->Map(tile_render_d3d11->tile_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
		&mapped_buffer);
	assert(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &tile_constant_buffer, sizeof(tile_constant_buffer));
	d3d11->Unmap(tile_render_d3d11->tile_constant_buffer, 0);

	d3d11->IASetInputLayout(NULL);
	d3d11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	d3d11->VSSetConstantBuffers(0, 1, &tile_render_d3d11->tile_constant_buffer);
	d3d11->VSSetShaderResources(0, 1, &tile_render_d3d11->tile_instance_buffer_srv);
	d3d11->VSSetShader(tile_render_d3d11->tile_vertex_shader, NULL, 0);
	d3d11->PSSetConstantBuffers(0, 1, &tile_render_d3d11->tile_constant_buffer);
	d3d11->PSSetShaderResources(0, 1, &tile_render_d3d11->tile_tex_srv);
	d3d11->PSSetShader(tile_render_d3d11->tile_pixel_shader, NULL, 0);

	d3d11->RSSetState(tile_render_d3d11->tile_rasterizer_state);
	d3d11->OMSetRenderTargets(1, &output_rtv, NULL);

	d3d11->DrawInstanced(6, tile_render->num_instances, 0, 0);
}

#endif // ifdef JFG_HEADER_ONLY

#endif // ifdef JFG_D3D11_H

#endif
