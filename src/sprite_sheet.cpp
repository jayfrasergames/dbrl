#define JFG_HEADER_ONLY
#include "jfg_d3d11.h"

#include "sprite_sheet.h"

#include "stdafx.h"

void sprite_sheet_renderer_init(Sprite_Sheet_Renderer* renderer,
                                Sprite_Sheet_Instances* instance_buffers,
                                u32 num_instance_buffers,
                                v2_u32 size)
{
	particles_init(&renderer->particles);
	renderer->size = size;
	renderer->num_instance_buffers = num_instance_buffers;
	renderer->instance_buffers = instance_buffers;
}

u32 sprite_sheet_renderer_id_in_pos(Sprite_Sheet_Renderer* renderer, v2_u32 pos)
{
	u32 best_id = 0;
	f32 best_depth = 0.0f;
	for (u32 i = 0; i < renderer->num_instance_buffers; ++i) {
		Sprite_Sheet_Instances* instances = &renderer->instance_buffers[i];
		u32 num_instances  = instances->num_instances;
		u32 tex_width      = instances->data.size.w;
		v2_u32 sprite_size = instances->data.sprite_size;
		for (u32 j = 0; j < num_instances; ++j) {
			Sprite_Sheet_Instance *instance = &instances->instances[j];
			v2_u32 top_left = (v2_u32)(instance->world_pos * (v2)sprite_size);
			top_left.y += (u32)instance->y_offset;
			if (top_left.x > pos.x || top_left.y > pos.y) {
				continue;
			}
			v2_u32 tile_coord = { pos.x - top_left.x, pos.y - top_left.y };
			if (tile_coord.x >= sprite_size.w || tile_coord.y >= sprite_size.h) {
				continue;
			}
			v2_u32 tex_coord = {
				((u32)instance->sprite_pos.x) * sprite_size.w + tile_coord.x,
				((u32)instance->sprite_pos.y) * sprite_size.h + tile_coord.y
			};
			u32 index = tex_coord.y * tex_width + tex_coord.x;
			u32 array_index = index >> 3;
			u32 bit_mask = 1 << (index & 7);
			if (!(instances->data.mouse_map_data[array_index] & bit_mask)) {
				continue;
			}
			f32 depth = instance->depth_offset + instance->world_pos.y;
			if (depth > best_depth) {
				best_id = instance->sprite_id;
				best_depth = depth;
			}
		}
	}
	return best_id;
}

void sprite_sheet_renderer_highlight_sprite(Sprite_Sheet_Renderer* renderer, u32 sprite_id)
{
	renderer->highlighted_sprite = sprite_id ? sprite_id : (u32)-1;
}

void sprite_sheet_instances_reset(Sprite_Sheet_Instances* instances)
{
	instances->num_instances = 0;
}

void sprite_sheet_instances_add(Sprite_Sheet_Instances* instances, Sprite_Sheet_Instance instance)
{
	ASSERT(instances->num_instances < SPRITE_SHEET_MAX_INSTANCES);
	instances->instances[instances->num_instances++] = instance;
}

void sprite_sheet_font_instances_reset(Sprite_Sheet_Font_Instances* instances)
{
	instances->instances.reset();
}

void sprite_sheet_font_instances_add(Sprite_Sheet_Font_Instances* instances,
                                     Sprite_Sheet_Font_Instance   instance)
{
	instances->instances.append(instance);
}

// D3D11

#include "gen/sprite_sheet_dxbc_vertex_shader.data.h"
#include "gen/sprite_sheet_dxbc_pixel_shader.data.h"
#include "gen/sprite_sheet_dxbc_clear_sprite_id_compute_shader.data.h"
#include "gen/sprite_sheet_dxbc_highlight_sprite_compute_shader.data.h"
#include "gen/sprite_sheet_dxbc_font_vertex_shader.data.h"
#include "gen/sprite_sheet_dxbc_font_pixel_shader.data.h"

u8 sprite_sheet_renderer_d3d11_init(Sprite_Sheet_Renderer* renderer,
                                    ID3D11Device*          device)
{
	HRESULT hr;
	ID3D11VertexShader *font_vertex_shader;
	hr = device->CreateVertexShader(SPRITE_SHEET_FONT_DXBC_VS,
	                                ARRAY_SIZE(SPRITE_SHEET_FONT_DXBC_VS),
	                                NULL,
	                                &font_vertex_shader);
	if (FAILED(hr)) {
		goto error_init_font_vertex_shader;
	}

	ID3D11PixelShader *font_pixel_shader;
	hr = device->CreatePixelShader(SPRITE_SHEET_FONT_DXBC_PS,
	                               ARRAY_SIZE(SPRITE_SHEET_FONT_DXBC_PS),
	                               NULL,
	                               &font_pixel_shader);
	if (FAILED(hr)) {
		goto error_init_font_pixel_shader;
	}

	ID3D11RasterizerState *font_rasterizer_state;
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.DepthClipEnable = FALSE;
		desc.ScissorEnable = FALSE;
		desc.MultisampleEnable = FALSE;
		desc.AntialiasedLineEnable = FALSE;

		hr = device->CreateRasterizerState(&desc, &font_rasterizer_state);
	}
	if (FAILED(hr)) {
		goto error_init_font_rasterizer_state;
	}

	ID3D11VertexShader *vertex_shader;
	hr = device->CreateVertexShader(SPRITE_SHEET_RENDER_DXBC_VS,
	                                ARRAY_SIZE(SPRITE_SHEET_RENDER_DXBC_VS),
	                                NULL,
	                                &vertex_shader);
	if (FAILED(hr)) {
		goto error_init_vertex_shader;
	}

	ID3D11PixelShader *pixel_shader;
	hr = device->CreatePixelShader(SPRITE_SHEET_RENDER_DXBC_PS,
	                               ARRAY_SIZE(SPRITE_SHEET_RENDER_DXBC_PS),
	                               NULL,
	                               &pixel_shader);
	if (FAILED(hr)) {
		goto error_init_pixel_shader;
	}

	ID3D11RasterizerState *rasterizer_state;
	{
		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;

		hr = device->CreateRasterizerState(&desc, &rasterizer_state);
	}
	if (FAILED(hr)) {
		goto error_init_rasterizer_state;
	}

	ID3D11Texture2D *output;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = renderer->size.w;
		desc.Height = renderer->size.h;
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

		hr = device->CreateTexture2D(&desc, NULL, &output);
	}
	if (FAILED(hr)) {
		goto error_init_output;
	}

	ID3D11RenderTargetView *output_rtv;
	{
		D3D11_RENDER_TARGET_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;

		hr = device->CreateRenderTargetView(output, &desc, &output_rtv);
	}
	if (FAILED(hr)) {
		goto error_init_output_rtv;
	}

	ID3D11UnorderedAccessView *output_uav;
	hr = device->CreateUnorderedAccessView(output, NULL, &output_uav);
	if (FAILED(hr)) {
		goto error_init_output_uav;
	}

	ID3D11ShaderResourceView *output_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MostDetailedMip = 0;
		desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(output, &desc, &output_srv);
	}
	if (FAILED(hr)) {
		goto error_init_output_srv;
	}

	ID3D11Texture2D *depth_buffer;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = renderer->size.w;
		desc.Height = renderer->size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_D16_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		hr = device->CreateTexture2D(&desc, NULL, &depth_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_depth_buffer;
	}

	ID3D11DepthStencilView *depth_buffer_dsv;
	{
		D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_D16_UNORM;
		desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		desc.Flags = 0;
		desc.Texture2D.MipSlice = 0;

		hr = device->CreateDepthStencilView(depth_buffer, &desc, &depth_buffer_dsv);
	}
	if (FAILED(hr)) {
		goto error_init_depth_buffer_dsv;
	}

	ID3D11DepthStencilState *depth_stencil_state;
	{
		D3D11_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_GREATER;
		desc.StencilEnable = FALSE;
		
		hr = device->CreateDepthStencilState(&desc, &depth_stencil_state);
	}
	if (FAILED(hr)) {
		goto error_init_depth_stencil_state;
	}

	ID3D11Texture2D *sprite_id_buffer;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = renderer->size.w;
		desc.Height = renderer->size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET
		                                            | D3D11_BIND_UNORDERED_ACCESS;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		hr = device->CreateTexture2D(&desc, NULL, &sprite_id_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_id_buffer;
	}

	ID3D11ShaderResourceView *sprite_id_buffer_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MostDetailedMip = 0;
		desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(sprite_id_buffer, &desc, &sprite_id_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_id_buffer_srv;
	}

	ID3D11UnorderedAccessView *sprite_id_buffer_uav;
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;

		hr = device->CreateUnorderedAccessView(sprite_id_buffer, &desc, &sprite_id_buffer_uav);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_id_buffer_uav;
	}

	ID3D11RenderTargetView *sprite_id_buffer_rtv;
	{
		D3D11_RENDER_TARGET_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_R32_UINT;
		desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		desc.Texture2D.MipSlice = 0;

		hr = device->CreateRenderTargetView(sprite_id_buffer, &desc, &sprite_id_buffer_rtv);
	}
	if (FAILED(hr)) {
		goto error_init_sprite_id_buffer_rtv;
	}

	ID3D11ComputeShader *clear_sprite_id_compute_shader;
	hr = device->CreateComputeShader(SPRITE_SHEET_CLEAR_SPRITE_ID_CS,
	                                 ARRAY_SIZE(SPRITE_SHEET_CLEAR_SPRITE_ID_CS),
	                                 NULL,
	                                 &clear_sprite_id_compute_shader);
	if (FAILED(hr)) {
		goto error_init_clear_sprite_id_compute_shader;
	}

	ID3D11ComputeShader *highlight_sprite_compute_shader;
	hr = device->CreateComputeShader(SPRITE_SHEET_HIGHLIGHT_CS,
	                                 ARRAY_SIZE(SPRITE_SHEET_HIGHLIGHT_CS),
	                                 NULL,
	                                 &highlight_sprite_compute_shader);
	if (FAILED(hr)) {
		goto error_init_highlight_sprite_compute_shader;
	}

	ID3D11Buffer *highlight_constant_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Sheet_Highlight_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Sprite_Sheet_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &highlight_constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_highlight_constant_buffer;
	}

	if (!particles_d3d11_init(&renderer->particles, device)) {
		goto error_init_particles;
	}

	renderer->d3d11.output               = output;
	renderer->d3d11.output_rtv           = output_rtv;
	renderer->d3d11.output_uav           = output_uav;
	renderer->d3d11.output_srv           = output_srv;
	renderer->d3d11.depth_buffer         = depth_buffer;
	renderer->d3d11.depth_buffer_dsv     = depth_buffer_dsv;
	renderer->d3d11.depth_stencil_state  = depth_stencil_state;
	renderer->d3d11.sprite_id_buffer     = sprite_id_buffer;
	renderer->d3d11.sprite_id_buffer_srv = sprite_id_buffer_srv;
	renderer->d3d11.sprite_id_buffer_uav = sprite_id_buffer_uav;
	renderer->d3d11.sprite_id_buffer_rtv = sprite_id_buffer_rtv;
	renderer->d3d11.vertex_shader        = vertex_shader;
	renderer->d3d11.pixel_shader         = pixel_shader;
	renderer->d3d11.rasterizer_state     = rasterizer_state;
	renderer->d3d11.clear_sprite_id_compute_shader = clear_sprite_id_compute_shader;
	renderer->d3d11.highlight_sprite_compute_shader = highlight_sprite_compute_shader;
	renderer->d3d11.highlight_constant_buffer = highlight_constant_buffer;
	renderer->d3d11.font_rasterizer_state = font_rasterizer_state;
	renderer->d3d11.font_pixel_shader    = font_pixel_shader;
	renderer->d3d11.font_vertex_shader   = font_vertex_shader;
	return 1;

	particles_d3d11_free(&renderer->particles);
error_init_particles:
	highlight_constant_buffer->Release();
error_init_highlight_constant_buffer:
	highlight_sprite_compute_shader->Release();
error_init_highlight_sprite_compute_shader:
	clear_sprite_id_compute_shader->Release();
error_init_clear_sprite_id_compute_shader:
	sprite_id_buffer_rtv->Release();
error_init_sprite_id_buffer_rtv:
	sprite_id_buffer_uav->Release();
error_init_sprite_id_buffer_uav:
	sprite_id_buffer_srv->Release();
error_init_sprite_id_buffer_srv:
	sprite_id_buffer->Release();
error_init_sprite_id_buffer:
	depth_stencil_state->Release();
error_init_depth_stencil_state:
	depth_buffer_dsv->Release();
error_init_depth_buffer_dsv:
	depth_buffer->Release();
error_init_depth_buffer:
	output_srv->Release();
error_init_output_srv:
	output_uav->Release();
error_init_output_uav:
	output_rtv->Release();
error_init_output_rtv:
	output->Release();
error_init_output:
	rasterizer_state->Release();
error_init_rasterizer_state:
	pixel_shader->Release();
error_init_pixel_shader:
	vertex_shader->Release();
error_init_vertex_shader:
	font_rasterizer_state->Release();
error_init_font_rasterizer_state:
	font_pixel_shader->Release();
error_init_font_pixel_shader:
	font_vertex_shader->Release();
error_init_font_vertex_shader:
	return 0;
}

void sprite_sheet_renderer_d3d11_free(Sprite_Sheet_Renderer* renderer)
{
	particles_d3d11_free(&renderer->particles);
	renderer->d3d11.highlight_constant_buffer->Release();
	renderer->d3d11.highlight_sprite_compute_shader->Release();
	renderer->d3d11.clear_sprite_id_compute_shader->Release();
	renderer->d3d11.sprite_id_buffer_rtv->Release();
	renderer->d3d11.sprite_id_buffer_uav->Release();
	renderer->d3d11.sprite_id_buffer_srv->Release();
	renderer->d3d11.sprite_id_buffer->Release();
	renderer->d3d11.depth_stencil_state->Release();
	renderer->d3d11.depth_buffer_dsv->Release();
	renderer->d3d11.depth_buffer->Release();
	renderer->d3d11.output_srv->Release();
	renderer->d3d11.output_uav->Release();
	renderer->d3d11.output_rtv->Release();
	renderer->d3d11.output->Release();
	renderer->d3d11.vertex_shader->Release();
	renderer->d3d11.pixel_shader->Release();
	renderer->d3d11.rasterizer_state->Release();
	renderer->d3d11.font_vertex_shader->Release();
	renderer->d3d11.font_pixel_shader->Release();
	renderer->d3d11.font_rasterizer_state->Release();
}

u8 sprite_sheet_instances_d3d11_init(Sprite_Sheet_Instances* instances, ID3D11Device* device)
{
	HRESULT hr;
	ID3D11Buffer *constant_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Sheet_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Sprite_Sheet_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_constant_buffer;
	}

	ID3D11Buffer *instance_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Sheet_Instance) * SPRITE_SHEET_MAX_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Sprite_Sheet_Instance);

		hr = device->CreateBuffer(&desc, NULL, &instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer;
	}

	ID3D11ShaderResourceView *instance_buffer_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_UNKNOWN;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv_desc.Buffer.ElementOffset = 0;
		srv_desc.Buffer.ElementWidth = SPRITE_SHEET_MAX_INSTANCES;

		hr = device->CreateShaderResourceView(instance_buffer,
				&srv_desc, &instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer_srv;
	}

	ID3D11Texture2D *texture;
	{
		D3D11_TEXTURE2D_DESC tex_desc = {};
		tex_desc.Width = instances->data.size.w;
		tex_desc.Height = instances->data.size.h;
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
		data_desc.pSysMem = instances->data.image_data;
		data_desc.SysMemPitch = instances->data.size.w * sizeof(u32);
		data_desc.SysMemSlicePitch = 0;

		hr = device->CreateTexture2D(&tex_desc, &data_desc, &texture);
	}
	if (FAILED(hr)) {
		goto error_init_texture;
	}

	ID3D11ShaderResourceView *texture_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(texture, &srv_desc, &texture_srv);
	}
	if (FAILED(hr)) {
		goto error_init_srv;
	}

	instances->d3d11.constant_buffer = constant_buffer;
	instances->d3d11.instance_buffer = instance_buffer;
	instances->d3d11.instance_buffer_srv = instance_buffer_srv;
	instances->d3d11.texture = texture;
	instances->d3d11.texture_srv = texture_srv;
	return 1;

	texture_srv->Release();
error_init_srv:
	texture->Release();
error_init_texture:
	instance_buffer_srv->Release();
error_init_instance_buffer_srv:
	instance_buffer->Release();
error_init_instance_buffer:
	constant_buffer->Release();
error_init_constant_buffer:
	return 0;
}

void sprite_sheet_instances_d3d11_free(Sprite_Sheet_Instances* instances)
{
	instances->d3d11.texture_srv->Release();
	instances->d3d11.texture->Release();
	instances->d3d11.instance_buffer_srv->Release();
	instances->d3d11.instance_buffer->Release();
	instances->d3d11.constant_buffer->Release();
}

u8 sprite_sheet_font_instances_d3d11_init(Sprite_Sheet_Font_Instances* instances, ID3D11Device* device)
{
	HRESULT hr;
	ID3D11Buffer *constant_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Sheet_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Sprite_Sheet_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_constant_buffer;
	}

	ID3D11Buffer *instance_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Sprite_Sheet_Font_Instance) * SPRITE_SHEET_MAX_INSTANCES;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(Sprite_Sheet_Font_Instance);

		hr = device->CreateBuffer(&desc, NULL, &instance_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer;
	}

	ID3D11ShaderResourceView *instance_buffer_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_UNKNOWN;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv_desc.Buffer.ElementOffset = 0;
		srv_desc.Buffer.ElementWidth = SPRITE_SHEET_MAX_INSTANCES;

		hr = device->CreateShaderResourceView(instance_buffer,
		                                      &srv_desc,
		                                      &instance_buffer_srv);
	}
	if (FAILED(hr)) {
		goto error_init_instance_buffer_srv;
	}

	ID3D11Texture2D *texture;
	{
		D3D11_TEXTURE2D_DESC tex_desc = {};
		tex_desc.Width = instances->tex_size.w;
		tex_desc.Height = instances->tex_size.h;
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
		data_desc.pSysMem = instances->tex_data;
		data_desc.SysMemPitch = instances->tex_size.w * sizeof(u32);
		data_desc.SysMemSlicePitch = 0;

		hr = device->CreateTexture2D(&tex_desc, &data_desc, &texture);
	}
	if (FAILED(hr)) {
		goto error_init_texture;
	}

	ID3D11ShaderResourceView *texture_srv;
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MostDetailedMip = 0;
		srv_desc.Texture2D.MipLevels = 1;

		hr = device->CreateShaderResourceView(texture, &srv_desc, &texture_srv);
	}
	if (FAILED(hr)) {
		goto error_init_srv;
	}

	instances->d3d11.constant_buffer = constant_buffer;
	instances->d3d11.instance_buffer = instance_buffer;
	instances->d3d11.instance_buffer_srv = instance_buffer_srv;
	instances->d3d11.texture = texture;
	instances->d3d11.texture_srv = texture_srv;
	return 1;

	texture_srv->Release();
error_init_srv:
	texture->Release();
error_init_texture:
	instance_buffer_srv->Release();
error_init_instance_buffer_srv:
	instance_buffer->Release();
error_init_instance_buffer:
	constant_buffer->Release();
error_init_constant_buffer:
	return 0;
}

void sprite_sheet_font_instances_d3d11_free(Sprite_Sheet_Font_Instances* instances)
{
	instances->d3d11.texture_srv->Release();
	instances->d3d11.texture->Release();
	instances->d3d11.instance_buffer_srv->Release();
	instances->d3d11.instance_buffer->Release();
	instances->d3d11.constant_buffer->Release();
}

void sprite_sheet_renderer_d3d11_begin(Sprite_Sheet_Renderer*  renderer,
                                       ID3D11DeviceContext*    dc)
{
	dc->ClearState();

	// set viewport
	{
		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width  = (f32)renderer->size.w;
		viewport.Height = (f32)renderer->size.h;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		dc->RSSetViewports(1, &viewport);
	}

	f32 clear_value[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(renderer->d3d11.output_rtv, clear_value);
	dc->ClearDepthStencilView(renderer->d3d11.depth_buffer_dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);

	dc->CSSetShader(renderer->d3d11.clear_sprite_id_compute_shader, NULL, 0);
	dc->CSSetUnorderedAccessViews(0, 1, &renderer->d3d11.sprite_id_buffer_uav, NULL);
	dc->Dispatch((renderer->size.w + CLEAR_SPRITE_ID_WIDTH  - 1) / CLEAR_SPRITE_ID_WIDTH,
	             (renderer->size.h + CLEAR_SPRITE_ID_HEIGHT - 1) / CLEAR_SPRITE_ID_HEIGHT,
	             1);
	ID3D11UnorderedAccessView *null_uav = NULL;
	dc->CSSetUnorderedAccessViews(0, 1, &null_uav, NULL);

	// dc->ClearState();
	dc->IASetInputLayout(NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->VSSetShader(renderer->d3d11.vertex_shader, NULL, 0);
	dc->PSSetShader(renderer->d3d11.pixel_shader, NULL, 0);
	dc->RSSetState(renderer->d3d11.rasterizer_state);

	dc->OMSetDepthStencilState(renderer->d3d11.depth_stencil_state, 0);
	ID3D11RenderTargetView *rtvs[] = {
		renderer->d3d11.output_rtv,
		renderer->d3d11.sprite_id_buffer_rtv,
	};
	dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, renderer->d3d11.depth_buffer_dsv);
}

void sprite_sheet_instances_d3d11_draw(Sprite_Sheet_Renderer* renderer,
                                       Sprite_Sheet_Instances* instances,
                                       ID3D11DeviceContext*    dc)
{
	Sprite_Sheet_Constant_Buffer constant_buffer = {};
	constant_buffer.screen_size = (v2)renderer->size;
	constant_buffer.sprite_size = (v2)instances->data.sprite_size;
	constant_buffer.world_tile_size = v2(24.0f, 24.0f);
	constant_buffer.tex_size = (v2)instances->data.size;

	D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};

	HRESULT hr;
	hr = dc->Map(instances->d3d11.instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData,
	       &instances->instances,
	       sizeof(Sprite_Sheet_Instance) * instances->num_instances);
	dc->Unmap(instances->d3d11.instance_buffer, 0);

	hr = dc->Map(instances->d3d11.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
		&mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(instances->d3d11.constant_buffer, 0);

	dc->PSSetShaderResources(0, 1, &instances->d3d11.texture_srv);
	dc->PSSetConstantBuffers(0, 1, &instances->d3d11.constant_buffer);
	dc->VSSetShaderResources(0, 1, &instances->d3d11.instance_buffer_srv);
	dc->VSSetConstantBuffers(0, 1, &instances->d3d11.constant_buffer);

	dc->DrawInstanced(6, instances->num_instances, 0, 0);
}

void sprite_sheet_renderer_d3d11_begin_font(Sprite_Sheet_Renderer*  renderer,
                                            ID3D11DeviceContext*    dc)
{
	dc->IASetInputLayout(NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->VSSetShader(renderer->d3d11.font_vertex_shader, NULL, 0);
	dc->PSSetShader(renderer->d3d11.font_pixel_shader, NULL, 0);
	dc->RSSetState(renderer->d3d11.font_rasterizer_state);

	dc->OMSetDepthStencilState(NULL, 0);
	dc->OMSetRenderTargets(1, &renderer->d3d11.output_rtv, NULL);
}

void sprite_sheet_font_instances_d3d11_draw(Sprite_Sheet_Renderer*       renderer,
                                            Sprite_Sheet_Font_Instances* instances,
                                            ID3D11DeviceContext*         dc)
{
	Sprite_Sheet_Constant_Buffer constant_buffer = {};
	constant_buffer.screen_size = (v2)renderer->size;
	// constant_buffer.sprite_size = (v2)instances->data.sprite_size;
	constant_buffer.world_tile_size = v2(24.0f, 24.0f);
	constant_buffer.tex_size = (v2)instances->tex_size;

	D3D11_MAPPED_SUBRESOURCE mapped_buffer = {};

	HRESULT hr;
	hr = dc->Map(instances->d3d11.instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData,
	       &instances->instances.items,
	       sizeof(instances->instances[0]) * instances->instances.len);
	dc->Unmap(instances->d3d11.instance_buffer, 0);

	hr = dc->Map(instances->d3d11.constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
		&mapped_buffer);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(instances->d3d11.constant_buffer, 0);

	dc->PSSetShaderResources(0, 1, &instances->d3d11.texture_srv);
	dc->PSSetConstantBuffers(0, 1, &instances->d3d11.constant_buffer);
	dc->VSSetShaderResources(0, 1, &instances->d3d11.instance_buffer_srv);
	dc->VSSetConstantBuffers(0, 1, &instances->d3d11.constant_buffer);

	dc->DrawInstanced(6, instances->instances.len, 0, 0);
}

void sprite_sheet_renderer_d3d11_highlight_sprite(Sprite_Sheet_Renderer* renderer,
                                                  ID3D11DeviceContext*   dc)
{
	ID3D11RenderTargetView *rtvs[] = { NULL, NULL };
	ID3D11ShaderResourceView *null_srv = NULL;
	ID3D11UnorderedAccessView *null_uav = NULL;

	dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
	dc->VSSetShaderResources(0, 1, &null_srv);
	dc->PSSetShaderResources(0, 1, &null_srv);

	Sprite_Sheet_Highlight_Constant_Buffer constant_buffer = {};
	constant_buffer.highlight_color = { 1.0f, 0.0f, 0.0f, 1.0f };
	constant_buffer.sprite_id = renderer->highlighted_sprite;
	HRESULT hr;

	D3D11_MAPPED_SUBRESOURCE mapped_resource;
	hr = dc->Map(renderer->d3d11.highlight_constant_buffer,
	             0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_resource.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(renderer->d3d11.highlight_constant_buffer, 0);

	dc->CSSetConstantBuffers(0, 1, &renderer->d3d11.highlight_constant_buffer);
	dc->CSSetShaderResources(0, 1, &renderer->d3d11.sprite_id_buffer_srv);
	dc->CSSetUnorderedAccessViews(0, 1, &renderer->d3d11.output_uav, NULL);
	dc->CSSetShader(renderer->d3d11.highlight_sprite_compute_shader, NULL, 0);

	dc->Dispatch((renderer->size.w + HIGHLIGHT_SPRITE_WIDTH  - 1) / HIGHLIGHT_SPRITE_WIDTH,
	             (renderer->size.h + HIGHLIGHT_SPRITE_HEIGHT - 1) / HIGHLIGHT_SPRITE_HEIGHT,
	             1);

	ID3D11Buffer *null_cb = NULL;
	dc->CSSetConstantBuffers(0, 1, &null_cb);
	dc->CSSetShaderResources(0, 1, &null_srv);
	dc->CSSetUnorderedAccessViews(0, 1, &null_uav, NULL);
	dc->CSSetShader(NULL, NULL, 0);
}

void sprite_sheet_renderer_d3d11_do_particles(Sprite_Sheet_Renderer* renderer,
                                              ID3D11DeviceContext* dc)
{
	// XXX - tile width/height assumed
	ID3D11RenderTargetView *rtvs[2] = {};
	ID3D11RenderTargetView *null_rtvs[2] = {};
	dc->OMGetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
	dc->OMSetRenderTargets(ARRAY_SIZE(null_rtvs), null_rtvs, NULL);

	particles_d3d11_draw(&renderer->particles,
	                     dc,
	                     renderer->d3d11.output_uav,
	                     { 24, 24 },
	                     renderer->time);

	dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
}

void sprite_sheet_renderer_d3d11_end(Sprite_Sheet_Renderer*  renderer,
                                     ID3D11DeviceContext*    dc)
{
	// TODO -- set the viewport

	dc->VSSetShader(NULL, NULL, 0);
	dc->PSSetShader(NULL, NULL, 0);

	ID3D11RenderTargetView *rtvs[] = { NULL, NULL };
	dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
	ID3D11ShaderResourceView *null_srv = NULL;
	dc->VSSetShaderResources(0, 1, &null_srv);
	dc->PSSetShaderResources(0, 1, &null_srv);
}

