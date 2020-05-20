#ifndef SPRITE_RENDER_H
#define SPRITE_RENDER_H

#include "jfg/prelude.h"
#include "sprite_render_gpu_data_types.h"

struct Sprite_Render_Texture
{
	v2_u32 dimensions;
	v2_u32 sprite_size;
	void *data;
};

#define SPRITE_RENDER_MAX_INSTANCES 4096

struct Sprite_Render
{
	Sprite_Render_Texture texture;
	u32 num_instances;
	Sprite_Render_Sprite_Instance sprite_instances[SPRITE_RENDER_MAX_INSTANCES];
};

void sprite_render_reset(Sprite_Render* context);
void sprite_render_init(Sprite_Render* context, Sprite_Render_Texture* texture);
void sprite_render_add_sprite(Sprite_Render* context, Sprite_Render_Sprite_Instance instance);

#ifndef JFG_HEADER_ONLY

// XXX - for memset, should probably get rid of this later
#include <string.h>

void sprite_render_reset(Sprite_Render* context)
{
	context->num_instances = 0;
}

void sprite_render_init(Sprite_Render* sprite_render, Sprite_Render_Texture* texture)
{
	memset(sprite_render, 0, sizeof(*sprite_render));
	sprite_render->texture = *texture;
}

void sprite_render_add_sprite(Sprite_Render* context, Sprite_Render_Sprite_Instance instance)
{
	context->sprite_instances[context->num_instances++] = instance;
}

#endif

// d3d11
#ifdef JFG_D3D11_H

// XXX - would be cooler to have our own assert in prelude
#include <assert.h>

struct Sprite_Render_D3D11_Context
{
	ID3D11Texture2D          *sprite_tex;
	ID3D11ShaderResourceView *sprite_tex_srv;
	ID3D11VertexShader       *sprite_vertex_shader;
	ID3D11PixelShader        *sprite_pixel_shader;
	ID3D11Buffer             *sprite_instance_buffer;
	ID3D11ShaderResourceView *sprite_instance_buffer_srv;
	ID3D11Buffer             *sprite_constant_buffer;
	ID3D11RasterizerState    *sprite_rasterizer_state;
};

u8 sprite_render_d3d11_init(Sprite_Render_D3D11_Context* context,
                          Sprite_Render*               sprite_render,
                          ID3D11Device*              device);

void sprite_render_d3d11_draw(Sprite_Render_D3D11_Context* sprite_render_d3d11,
                            Sprite_Render*               sprite_render,
                            ID3D11DeviceContext*       d3d11,
                            ID3D11RenderTargetView*    output_rtv,
                            v2                         screen_size);

#ifndef JFG_HEADER_ONLY
#include "gen/sprite_render_dxbc_sprite_vertex_shader.data.h"
#include "gen/sprite_render_dxbc_sprite_pixel_shader.data.h"

u8 sprite_render_d3d11_init(Sprite_Render_D3D11_Context* context,
                          Sprite_Render*               sprite_render,
                          ID3D11Device*              device)
{
	Sprite_Render_Texture header = sprite_render->texture;
	HRESULT hr;
	ID3D11Texture2D *sprite_tex;
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

		hr = device->CreateTexture2D(&tex_desc, &data_desc, &sprite_tex);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_tex;
	}

	ID3D11ShaderResourceView *sprite_tex_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(sprite_tex, &srv_desc, &sprite_tex_srv);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_tex_srv;
	}

	ID3D11VertexShader *sprite_vertex_shader;
	hr = device->CreateVertexShader(SPRITE_RENDER_SPRITE_VS, ARRAY_SIZE(SPRITE_RENDER_SPRITE_VS), NULL,
		&sprite_vertex_shader);
	if (FAILED(hr)) {
		goto error_init_sprite_vertex_shader;
	}

	ID3D11PixelShader *sprite_pixel_shader;
	hr = device->CreatePixelShader(SPRITE_RENDER_SPRITE_PS, ARRAY_SIZE(SPRITE_RENDER_SPRITE_PS), NULL,
		&sprite_pixel_shader);
	if (FAILED(hr)) {
		goto error_init_sprite_pixel_shader;
	}

	ID3D11Buffer *sprite_instance_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Render_Sprite_Instance) * SPRITE_RENDER_MAX_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Sprite_Render_Sprite_Instance);

		hr = device->CreateBuffer(&desc, NULL, &sprite_instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_instance_buffer;
	}

	ID3D11ShaderResourceView *sprite_instance_buffer_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_UNKNOWN;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv_desc.Buffer.ElementOffset = 0;
		srv_desc.Buffer.ElementWidth = SPRITE_RENDER_MAX_INSTANCES;

		hr = device->CreateShaderResourceView(sprite_instance_buffer,
			&srv_desc, &sprite_instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_instance_buffer_srv;
	}

	ID3D11Buffer *sprite_constant_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Render_CB_Sprite);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Sprite_Render_CB_Sprite);

		hr = device->CreateBuffer(&desc, NULL, &sprite_constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_constant_buffer;
	}

	ID3D11RasterizerState *sprite_rasterizer_state;
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;

		hr = device->CreateRasterizerState(&desc, &sprite_rasterizer_state);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_rasterizer_state;
	}

	context->sprite_tex                 = sprite_tex;
	context->sprite_tex_srv             = sprite_tex_srv;
	context->sprite_vertex_shader       = sprite_vertex_shader;
	context->sprite_pixel_shader        = sprite_pixel_shader;
	context->sprite_instance_buffer     = sprite_instance_buffer;
	context->sprite_instance_buffer_srv = sprite_instance_buffer_srv;
	context->sprite_constant_buffer     = sprite_constant_buffer;
	context->sprite_rasterizer_state    = sprite_rasterizer_state;

	return 1;

	sprite_rasterizer_state->Release();
error_init_sprite_rasterizer_state:
	sprite_constant_buffer->Release();
error_init_sprite_constant_buffer:
	sprite_instance_buffer_srv->Release();
error_init_sprite_instance_buffer_srv:
	sprite_instance_buffer->Release();
error_init_sprite_instance_buffer:
	sprite_pixel_shader->Release();
error_init_sprite_pixel_shader:
	sprite_vertex_shader->Release();
error_init_sprite_vertex_shader:
	sprite_tex_srv->Release();
error_init_sprite_tex_srv:
	sprite_tex->Release();
error_init_sprite_tex:
	return 0;
}

void sprite_render_d3d11_draw(Sprite_Render_D3D11_Context* sprite_render_d3d11,
                            Sprite_Render*               sprite_render,
                            ID3D11DeviceContext*       d3d11,
                            ID3D11RenderTargetView*    output_rtv,
                            v2                         screen_size)
{
	Sprite_Render_CB_Sprite sprite_constant_buffer = {};
	sprite_constant_buffer.screen_size = screen_size;
	// sprite_constant_buffer.screen_size.h = screen_size.h
	sprite_constant_buffer.sprite_size.w = sprite_render->texture.sprite_size.w;
	sprite_constant_buffer.sprite_size.h = sprite_render->texture.sprite_size.h;
	sprite_constant_buffer.tex_size.w = sprite_render->texture.dimensions.w;
	sprite_constant_buffer.tex_size.h = sprite_render->texture.dimensions.h;
	sprite_constant_buffer.zoom = 4.0f;

	D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};

	HRESULT hr;
	hr = d3d11->Map(sprite_render_d3d11->sprite_instance_buffer,
		0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	assert(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &sprite_render->sprite_instances,
		sizeof(Sprite_Render_Sprite_Instance) * sprite_render->num_instances);
	d3d11->Unmap(sprite_render_d3d11->sprite_instance_buffer, 0);

	hr = d3d11->Map(sprite_render_d3d11->sprite_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
		&mapped_buffer);
	assert(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &sprite_constant_buffer, sizeof(sprite_constant_buffer));
	d3d11->Unmap(sprite_render_d3d11->sprite_constant_buffer, 0);

	d3d11->IASetInputLayout(NULL);
	d3d11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	d3d11->VSSetConstantBuffers(0, 1, &sprite_render_d3d11->sprite_constant_buffer);
	d3d11->VSSetShaderResources(0, 1, &sprite_render_d3d11->sprite_instance_buffer_srv);
	d3d11->VSSetShader(sprite_render_d3d11->sprite_vertex_shader, NULL, 0);
	d3d11->PSSetConstantBuffers(0, 1, &sprite_render_d3d11->sprite_constant_buffer);
	d3d11->PSSetShaderResources(0, 1, &sprite_render_d3d11->sprite_tex_srv);
	d3d11->PSSetShader(sprite_render_d3d11->sprite_pixel_shader, NULL, 0);

	d3d11->RSSetState(sprite_render_d3d11->sprite_rasterizer_state);
	d3d11->OMSetRenderTargets(1, &output_rtv, NULL);

	d3d11->DrawInstanced(6, sprite_render->num_instances, 0, 0);
}

#endif // ifdef JFG_HEADER_ONLY

#endif // ifdef JFG_D3D11_H

#endif
