#include "draw_dx11.h"

#include "stdafx.h"
#include "jfg_d3d11.h"
#include "draw.h"
#include "shader_global_constants.h"

#define D3DCompileFromFile _D3DCompileFromFile
#include "d3dcompiler.h"
#undef D3DCompileFromFile

#define D3D_COMPILE_FUNCS \
	D3D_COMPILE_FUNC(HRESULT, D3DCompileFromFile, LPCWSTR pFileName, const D3D_SHADER_MACRO *pDefines, ID3DInclude *pInclude, LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs)

#define D3D_COMPILE_FUNC(return_type, name, ...) return_type (WINAPI *name)(__VA_ARGS__) = NULL;
D3D_COMPILE_FUNCS
#undef D3D_COMPILE_FUNC

#define SAFE_RELEASE(x) do { if (x) { x->Release(); x = NULL; } } while(0)

#define CONSTANT_BUFFER_SIZE 1024
#define MAX_INSTANCE_WIDTH   64
#define MAX_INSTANCES        64*1024
#define INSTANCE_BUFFER_SIZE MAX_INSTANCE_WIDTH * MAX_INSTANCES

static HMODULE d3d_compiler_library = NULL;
const char* D3D_COMPILER_DLL_NAME = "D3DCompiler_47.dll";
const char* SHADER_DIR = "..\\assets\\shaders";

enum Shader_Type
{
	PIXEL_SHADER,
	VERTEX_SHADER,
	COMPUTE_SHADER,
};

struct Shader_Metadata
{
	const char* name;
	const char* filename;
	const char* entry_point;
	const char* shader_model;
	u32 index;
	Shader_Type type;
};

const Shader_Metadata SHADER_METADATA[] = {
#define DX11_PS(name, filename, entry_point) { #name, filename, entry_point, "ps_5_0", (u32)DX11_PS_##name, PIXEL_SHADER },
	DX11_PIXEL_SHADERS
#undef DX11_PS
#define DX11_VS(name, filename, entry_point) { #name, filename, entry_point, "vs_5_0", (u32)DX11_VS_##name, VERTEX_SHADER },
	DX11_VERTEX_SHADERS
#undef DX11_VS
#define DX11_CS(name, filename, entry_point) { #name, filename, entry_point, "cs_5_0", (u32)DX11_CS_##name, COMPUTE_SHADER },
	DX11_COMPUTE_SHADERS
#undef DX11_CS
};

static void set_name(ID3D11DeviceChild* object, const char* name)
{
	size_t len = strlen(name);
	object->SetPrivateData(WKPDID_D3DDebugObjectName, len, name);
}

bool reload_shaders(DX11_Renderer* renderer)
{
	ID3D11Device *device = renderer->device;

	for (u32 i = 0; i < ARRAY_SIZE(SHADER_METADATA); ++i) {
		auto metadata = &SHADER_METADATA[i];
		auto filename = fmt("%s\\%s", SHADER_DIR, metadata->filename);
		wchar_t filename_wide[4096] = {};
		mbstowcs(filename_wide, filename, ARRAY_SIZE(filename_wide));

		ID3DBlob *code = NULL, *error_messages = NULL;

		HRESULT hr = D3DCompileFromFile(filename_wide,
		                                NULL,
		                                D3D_COMPILE_STANDARD_FILE_INCLUDE,
		                                metadata->entry_point,
		                                metadata->shader_model,
		                                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
		                                0,
		                                &code,
		                                &error_messages);

		if (FAILED(hr)) {
			// XXX -- check if error_messages == NULL and if so make an error message based on the HRESULT
			MessageBox(NULL, (char*)error_messages->GetBufferPointer(), "dbrl", MB_OK);
			printf("Failed to compile shader \"%s\"!\nErrors:\n%s\n",
			       metadata->name,
			       (char*)error_messages->GetBufferPointer());
			return false;
		}

		switch (metadata->type) {
		case PIXEL_SHADER: {
			ID3D11PixelShader *ps = NULL;
			hr = device->CreatePixelShader(code->GetBufferPointer(),
			                               code->GetBufferSize(),
			                               NULL,
			                               &ps);
			if (FAILED(hr)) {
				return false;
			}
			if (renderer->ps[metadata->index]) {
				renderer->ps[metadata->index]->Release();
			}
			renderer->ps[metadata->index] = ps;
			set_name(ps, metadata->name);
			break;
		}
		case VERTEX_SHADER: {
			ID3D11VertexShader *vs = NULL;
			hr = device->CreateVertexShader(code->GetBufferPointer(),
			                                code->GetBufferSize(),
			                                NULL,
			                                &vs);
			if (FAILED(hr)) {
				return false;
			}
			if (renderer->vs[metadata->index]) {
				renderer->vs[metadata->index]->Release();
			}
			renderer->vs[metadata->index] = vs;
			set_name(vs, metadata->name);
			renderer->vs_code[metadata->index] = code;
			break;
		}
		case COMPUTE_SHADER: {
			ID3D11ComputeShader *cs = NULL;
			hr = device->CreateComputeShader(code->GetBufferPointer(),
			                                 code->GetBufferSize(),
			                                 NULL,
			                                 &cs);
			if (FAILED(hr)) {
				return false;
			}
			if (renderer->cs[metadata->index]) {
				renderer->cs[metadata->index]->Release();
			}
			renderer->cs[metadata->index] = cs;
			set_name(cs, metadata->name);
			break;
		}
		}

	}

	return true;
}

bool reload_textures(DX11_Renderer* dx11_render, Render* render)
{
	for (u32 i = 0; i < dx11_render->tex.len; ++i) {
		if (dx11_render->tex[i]) {
			dx11_render->tex[i]->Release();
		}
	}

	dx11_render->tex.reset();

	for (u32 i = 0; i < render->textures.len; ++i) {
		auto tex = &render->textures[i];

		D3D11_TEXTURE2D_DESC tex_desc = {};
		tex_desc.Width = tex->texture->size.w;
		tex_desc.Height = tex->texture->size.h;
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
		data_desc.pSysMem = tex->texture->data;
		data_desc.SysMemPitch = tex->texture->size.w * sizeof(u32);
		data_desc.SysMemSlicePitch = 0;

		ID3D11Texture2D *out_tex = NULL;
		HRESULT hr = dx11_render->device->CreateTexture2D(&tex_desc, &data_desc, &out_tex);
		if (FAILED(hr)) {
			// TODO -- cleanup!!
			// or do we just quietly fail...?
			return false;
		}

		set_name(out_tex, fmt("TEXTURE(%s)", render->textures[i].filename));

		dx11_render->tex.append(out_tex);

		ID3D11ShaderResourceView *srv = NULL;

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;
		srv_desc.Texture2D.MostDetailedMip = 0;

		hr = dx11_render->device->CreateShaderResourceView(out_tex, &srv_desc, &srv);
		if (FAILED(hr)) {
			return false;
		}

		set_name(srv, fmt("SRV(%s)", render->textures[i].filename));

		dx11_render->srvs.append(srv);
	}

	return true;
}

static v2 screen_pos_to_world_pos(Camera* camera, v2_u32 screen_size, v2_u32 screen_pos)
{
	v2 p = (v2)screen_pos - ((v2)screen_size / 2.0f);
	f32 raw_zoom = (f32)screen_size.y / (camera->zoom * 24.0f);
	v2 world_pos = p / raw_zoom + 24.0f * (camera->world_center + camera->offset);
	return world_pos;
}

template<typename T>
static void push_instance(void* buffer, T* instance, u32* cur_pos)
{
	u32 idx = *cur_pos;
	*cur_pos += 1;
	ASSERT(sizeof(*instance) < MAX_INSTANCE_WIDTH);
	ASSERT(idx < MAX_INSTANCES);
	memcpy((void*)((uptr)buffer + idx * MAX_INSTANCE_WIDTH), instance, sizeof(*instance));
}

void draw(DX11_Renderer* renderer, Draw* draw, Render* render)
{
	Render_Job_Buffer *render_jobs = &render->render_job_buffer;

	v2_u32 screen_size = renderer->screen_size;

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (f32)screen_size.w;
	viewport.Height = (f32)screen_size.h;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	ID3D11DeviceContext *dc = renderer->device_context;

	dc->RSSetViewports(1, &viewport);

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(renderer->back_buffer_rtv, clear_color);
	dc->ClearRenderTargetView(renderer->output_rtv, clear_color);

	fov_render_d3d11_draw(&draw->fov_render, dc);
	sprite_sheet_renderer_d3d11_begin(&draw->renderer, dc);
	sprite_sheet_instances_d3d11_draw(&draw->renderer, &draw->tiles, dc);
	sprite_sheet_instances_d3d11_draw(&draw->renderer, &draw->creatures, dc);
	sprite_sheet_instances_d3d11_draw(&draw->renderer, &draw->water_edges, dc);
	sprite_sheet_renderer_d3d11_do_particles(&draw->renderer, dc);
	sprite_sheet_instances_d3d11_draw(&draw->renderer, &draw->effects_24, dc);
	sprite_sheet_instances_d3d11_draw(&draw->renderer, &draw->effects_32, dc);
	sprite_sheet_renderer_d3d11_highlight_sprite(&draw->renderer, dc);
	sprite_sheet_renderer_d3d11_begin_font(&draw->renderer, dc);
	sprite_sheet_font_instances_d3d11_draw(&draw->renderer, &draw->boxy_bold, dc);
	sprite_sheet_renderer_d3d11_end(&draw->renderer, dc);
	fov_render_d3d11_composite(&draw->fov_render,
	                           dc,
	                           draw->renderer.d3d11.output_uav,
	                           draw->renderer.size);

	v2 world_tl = screen_pos_to_world_pos(&draw->camera,
	                                      screen_size,
	                                      { 0, 0 });
	v2 world_br = screen_pos_to_world_pos(&draw->camera,
	                                      screen_size,
	                                      screen_size);
	v2 input_size = world_br - world_tl;
	pixel_art_upsampler_d3d11_draw(&renderer->pixel_art_upsampler,
	                               dc,
	                               draw->renderer.d3d11.output_srv,
	                               renderer->output_uav,
	                               input_size,
	                               world_tl,
	                               (v2)screen_size,
	                               v2(0.0f, 0.0f));

	card_render_d3d11_draw(&draw->card_render,
	                       dc,
	                       screen_size,
	                       renderer->output_rtv);
	// debug_line_d3d11_draw(&draw->card_debug_line, dc, renderer->output_rtv);

	v2 debug_zoom = (v2)(world_br - world_tl) / 24.0f;
	debug_draw_world_d3d11_draw(&draw->debug_draw_world,
	                            dc,
	                            renderer->output_rtv,
	                            draw->camera.world_center + draw->camera.offset,
	                            debug_zoom);

	imgui_d3d11_draw(&draw->imgui, dc, renderer->output_rtv, screen_size);

	HRESULT hr;
	u32 cur_cb = 0;
	ID3D11Buffer        **cbs = renderer->cbs;
	ID3D11Buffer        **dispatch_cbs = renderer->dispatch_cbs;
	ID3D11VertexShader  **vs  = renderer->vs;
	ID3D11PixelShader   **ps  = renderer->ps;
	ID3D11ComputeShader **cs  = renderer->cs;
	// ID3D11Texture2D     **tex = renderer->tex.items;
	ID3D11ShaderResourceView **srvs = renderer->srvs.items;

	// create "global" constant buffer
	ID3D11Buffer *global_cb = NULL;
	{
		Shader_Global_Constants global_constants = {};
		global_constants.screen_size = (v2)screen_size;

		global_cb = cbs[cur_cb++];
		D3D11_MAPPED_SUBRESOURCE mapped_res = {};
		hr = dc->Map(global_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
		ASSERT(SUCCEEDED(hr));
		memcpy(mapped_res.pData, &global_constants, sizeof(global_constants));
		dc->Unmap(global_cb, 0);
	}

	u32 cur_instance = 0;
	D3D11_MAPPED_SUBRESOURCE instance_map = {};
	hr = dc->Map(renderer->instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &instance_map);
	ASSERT(SUCCEEDED(hr));
	void *instance_buffer = instance_map.pData;

	u32 sprite_instance_buffer_offset = 0;

	render_jobs->cur_pos = render_jobs->base;
	for (auto job = (Render_Job*)render_jobs->base; job->type != RENDER_JOB_NONE; job = next_job(render_jobs)) {
		dc->ClearState();
		switch (job->type) {
		case RENDER_JOB_NONE:
			ASSERT(0);
			break;
		case RENDER_JOB_TRIANGLES: {
			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			ID3D11Buffer *dispatch_cb =  dispatch_cbs[cur_cb];
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};
			hr = dc->Map(dispatch_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			auto dispatch_constants = (Shader_Per_Dispatch_Constants*)mapped_res.pData;
			dispatch_constants->base_offset = cur_instance;
			dc->Unmap(dispatch_cb, 0);

			/*
			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->triangles.constants, sizeof(job->triangles.constants));
			dc->Unmap(cb, 0);
			*/

			++cur_cb;

			u32 len = job->triangles.instances.len;
			auto *instances = job->triangles.instances.items;
			for (u32 i = 0; i < len; ++i) {
				push_instance(instance_buffer, &instances[i], &cur_instance);
			}

			ID3D11Buffer *cbs[] = { global_cb, dispatch_cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShaderResources(0, 1, &renderer->instance_buffer_srv);
			dc->VSSetShader(vs[DX11_VS_TRIANGLE], NULL, 0);

			dc->RSSetViewports(1, &viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetShader(ps[DX11_PS_TRIANGLE], NULL, 0);

			ID3D11RenderTargetView *rtvs = { renderer->output_rtv };
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), &rtvs, NULL);
			dc->OMSetBlendState(renderer->blend_state, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(3, job->triangles.instances.len, 0, 0);
			break;
		}

		case RENDER_JOB_TYPE_SPRITES: {
			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->IASetInputLayout(renderer->sprite_input_layout);
			u32 stride = 32;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->sprite_instance_buffer, &stride, &offset);

			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			ID3D11Buffer *dispatch_cb =  dispatch_cbs[cur_cb];
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};
			hr = dc->Map(dispatch_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			auto dispatch_constants = (Shader_Per_Dispatch_Constants*)mapped_res.pData;
			dispatch_constants->base_offset = cur_instance;
			dc->Unmap(dispatch_cb, 0);

			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->sprites.constants, sizeof(job->sprites.constants));
			dc->Unmap(cb, 0);

			hr = dc->Map(renderer->sprite_instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			u32 len = job->sprites.instances.len;
			u32 sprite_size = sizeof(job->sprites.instances.items[0]);
			memcpy((void*)((uptr)mapped_res.pData + sprite_instance_buffer_offset * sprite_size),
			       job->sprites.instances.items,
			       len * sprite_size);
			dc->Unmap(renderer->sprite_instance_buffer, 0);

			/*
			u32 len = job->sprites.instances.len;
			auto *instances = job->sprites.instances.items;
			for (u32 i = 0; i < len; ++i) {
				push_instance(instance_buffer, &instances[i], &cur_instance);
			}
			*/

			ID3D11Buffer *cbs[] = { global_cb, dispatch_cb, cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_SPRITE], NULL, 0);

			dc->RSSetViewports(1, &viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetShaderResources(0, 1, &srvs[job->sprites.sprite_sheet_id]);
			dc->PSSetShader(ps[DX11_PS_SPRITE], NULL, 0);

			ID3D11RenderTargetView *rtvs = { renderer->output_rtv };
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), &rtvs, NULL);
			dc->OMSetBlendState(renderer->blend_state, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(6, job->sprites.instances.len, 0, sprite_instance_buffer_offset);
			sprite_instance_buffer_offset += len;
			break;
		}

		}
	}

	dc->Unmap(renderer->instance_buffer, 0);

	dc->ClearState();

	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->RSSetViewports(1, &viewport);

	dc->VSSetShader(renderer->vs[DX11_VS_PASS_THROUGH], NULL, 0);
	dc->PSSetShader(renderer->ps[DX11_PS_PASS_THROUGH], NULL, 0);
	dc->OMSetRenderTargets(1, &renderer->back_buffer_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &renderer->output_srv);
	dc->Draw(6, 0);

	renderer->swap_chain->Present(1, 0);
}

static bool program_d3d11_init(Draw* draw, ID3D11Device* device)
{
	if (!sprite_sheet_renderer_d3d11_init(&draw->renderer, device)) {
		goto error_init_sprite_sheet_renderer;
	}

	if (!sprite_sheet_instances_d3d11_init(&draw->tiles, device)) {
		goto error_init_sprite_sheet_tiles;
	}

	if (!sprite_sheet_instances_d3d11_init(&draw->creatures, device)) {
		goto error_init_sprite_sheet_creatures;
	}

	if (!sprite_sheet_instances_d3d11_init(&draw->water_edges, device)) {
		goto error_init_sprite_sheet_water_edges;
	}

	if (!sprite_sheet_instances_d3d11_init(&draw->effects_24, device)) {
		goto error_init_sprite_sheet_effects_24;
	}

	if (!sprite_sheet_instances_d3d11_init(&draw->effects_32, device)) {
		goto error_init_sprite_sheet_effects_32;
	}

	if (!sprite_sheet_font_instances_d3d11_init(&draw->boxy_bold, device)) {
		goto error_init_sprite_sheet_font_boxy_bold;
	}

	if (!imgui_d3d11_init(&draw->imgui, device)) {
		goto error_init_imgui;
	}

	if (!card_render_d3d11_init(&draw->card_render, device)) {
		goto error_init_card_render;
	}

	if (!debug_line_d3d11_init(&draw->card_debug_line, device)) {
		goto error_init_debug_line;
	}

	if (!debug_draw_world_d3d11_init(&draw->debug_draw_world, device)) {
		goto error_init_debug_draw_world;
	}

	if (!fov_render_d3d11_init(&draw->fov_render, device)) {
		goto error_init_fov_render;
	}

	return true;

	fov_render_d3d11_free(&draw->fov_render);
error_init_fov_render:
	debug_draw_world_d3d11_free(&draw->debug_draw_world);
error_init_debug_draw_world:
	debug_line_d3d11_free(&draw->card_debug_line);
error_init_debug_line:
	card_render_d3d11_free(&draw->card_render);
error_init_card_render:
	imgui_d3d11_free(&draw->imgui);
error_init_imgui:
	sprite_sheet_font_instances_d3d11_free(&draw->boxy_bold);
error_init_sprite_sheet_font_boxy_bold:
	sprite_sheet_instances_d3d11_free(&draw->effects_32);
error_init_sprite_sheet_effects_32:
	sprite_sheet_instances_d3d11_free(&draw->effects_24);
error_init_sprite_sheet_effects_24:
	sprite_sheet_instances_d3d11_free(&draw->water_edges);
error_init_sprite_sheet_water_edges:
	sprite_sheet_instances_d3d11_free(&draw->creatures);
error_init_sprite_sheet_creatures:
	sprite_sheet_instances_d3d11_free(&draw->tiles);
error_init_sprite_sheet_tiles:
	sprite_sheet_renderer_d3d11_free(&draw->renderer);
error_init_sprite_sheet_renderer:
	return false;
}

static bool create_output_texture(DX11_Renderer* renderer, v2_u32 size)
{
	HRESULT hr;
	ID3D11Texture2D *output_texture;
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = size.w;
		desc.Height = size.h;
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

		hr = renderer->device->CreateTexture2D(&desc, NULL, &output_texture);
	}
	if (FAILED(hr)) {
		goto error_init_output_texture;
	}

	ID3D11UnorderedAccessView *output_uav;
	hr = renderer->device->CreateUnorderedAccessView(output_texture, NULL, &output_uav);
	if (FAILED(hr)) {
		goto error_init_output_uav;
	}

	ID3D11RenderTargetView *output_rtv;
	hr = renderer->device->CreateRenderTargetView(output_texture, NULL, &output_rtv);
	if (FAILED(hr)) {
		goto error_init_output_rtv;
	}

	ID3D11ShaderResourceView *output_srv;
	hr = renderer->device->CreateShaderResourceView(output_texture, NULL, &output_srv);
	if (FAILED(hr)) {
		goto error_init_output_srv;
	}

	SAFE_RELEASE(renderer->output_uav);
	SAFE_RELEASE(renderer->output_rtv);
	SAFE_RELEASE(renderer->output_srv);
	SAFE_RELEASE(renderer->output_texture);

	renderer->max_screen_size = size;
	renderer->output_uav = output_uav;
	renderer->output_rtv = output_rtv;
	renderer->output_srv = output_srv;
	renderer->output_texture = output_texture;
	return true;

	output_srv->Release();
error_init_output_srv:
	output_rtv->Release();
error_init_output_rtv:
	output_uav->Release();
error_init_output_uav:
	output_texture->Release();
error_init_output_texture:
	return false;
}

JFG_Error init(DX11_Renderer* renderer, Log* log, Draw* draw, HWND window)
{
	renderer->log = log;

	if (!d3d11_try_load()) {
		jfg_set_error("Failed to load d3d11 DLL!");
		return JFG_ERROR;
	}

	d3d_compiler_library = LoadLibraryA("D3DCompiler_47.dll");
	if (!d3d_compiler_library) {
		jfg_set_error("Failed to load d3d compiler DLL!");
		return JFG_ERROR;
	}

#define D3D_COMPILE_FUNC(return_type, name, ...) \
	name = (return_type (WINAPI *)(__VA_ARGS__))GetProcAddress(d3d_compiler_library, #name);
	D3D_COMPILE_FUNCS
#undef D3D_COMPILE_FUNC

	D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };

	HRESULT hr = D3D11CreateDevice(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D11_CREATE_DEVICE_DEBUG,
		feature_levels,
		ARRAY_SIZE(feature_levels),
		D3D11_SDK_VERSION,
		&renderer->device,
		NULL,
		&renderer->device_context);

	if (FAILED(hr)) {
		jfg_set_error("Failed to create D3D11 device!");
		return JFG_ERROR;
	}

	ID3D11Device *device = renderer->device;

	if (!reload_shaders(renderer)) {
		return JFG_ERROR;
	}

	ID3D11InfoQueue *info_queue;
	hr = device->QueryInterface(IID_PPV_ARGS(&info_queue));

	if (FAILED(hr)) {
		jfg_set_error("Failed to create info queue!");
		return JFG_ERROR;
	}
	renderer->info_queue = info_queue;

	info_queue->SetMuteDebugOutput(FALSE);

	IDXGIDevice *dxgi_device;
	hr = device->QueryInterface(IID_PPV_ARGS(&dxgi_device));

	if (FAILED(hr)) {
		jfg_set_error("Failed to create DXGI device!");
		return JFG_ERROR;
	}

	IDXGIAdapter *dxgi_adapter;
	hr = dxgi_device->GetAdapter(&dxgi_adapter);

	if (FAILED(hr)) {
		return JFG_ERROR;
	}

	IDXGIFactory *dxgi_factory;
	hr = dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));

	if (FAILED(hr)) {
		return JFG_ERROR;
	}

	DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
	swap_chain_desc.BufferDesc.Width = 0;
	swap_chain_desc.BufferDesc.Height = 0;
	swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
	swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
	swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount = 2;
	swap_chain_desc.OutputWindow = window;
	swap_chain_desc.Windowed = TRUE;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	hr = dxgi_factory->CreateSwapChain(device, &swap_chain_desc, &renderer->swap_chain);

	if (FAILED(hr)) {
		return JFG_ERROR;
	}

	hr = renderer->swap_chain->GetBuffer(0, IID_PPV_ARGS(&renderer->back_buffer));
	if (FAILED(hr)) {
		return JFG_ERROR;
	}

	D3D11_TEXTURE2D_DESC back_buffer_desc = {};
	renderer->back_buffer->GetDesc(&back_buffer_desc);

	D3D11_RENDER_TARGET_VIEW_DESC back_buffer_rtv_desc = {};
	back_buffer_rtv_desc.Format = back_buffer_desc.Format;
	back_buffer_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	back_buffer_rtv_desc.Texture2D.MipSlice = 0;

	hr = device->CreateRenderTargetView(renderer->back_buffer,
	                                    &back_buffer_rtv_desc,
	                                    &renderer->back_buffer_rtv);
	if (FAILED(hr)) {
		return JFG_ERROR;
	}

	D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.Width = (f32)back_buffer_desc.Width;
	viewport.Height = (f32)back_buffer_desc.Height;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	auto screen_size = v2_u32(back_buffer_desc.Width, back_buffer_desc.Height);
	if (!create_output_texture(renderer, screen_size)) {
		return JFG_ERROR;
	}
	renderer->screen_size = screen_size;

	if (!program_d3d11_init(draw, renderer->device)) {
		return JFG_ERROR;
	}

	if (!pixel_art_upsampler_d3d11_init(&renderer->pixel_art_upsampler, renderer->device)) {
		return JFG_ERROR;
	}

	// create constant buffers
	for (u32 i = 0; i < ARRAY_SIZE(renderer->cbs); ++i) {
		ID3D11Buffer *cb = NULL;

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = CONSTANT_BUFFER_SIZE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		hr = device->CreateBuffer(&desc, NULL, &cb);
		if (FAILED(hr)) {
			return JFG_ERROR;
		}

		renderer->cbs[i] = cb;
	}

	// create per dispatch constant buffers
	for (u32 i = 0; i < ARRAY_SIZE(renderer->dispatch_cbs); ++i) {
		ID3D11Buffer *cb = NULL;

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = align(sizeof(Shader_Per_Dispatch_Constants), 16);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		hr = device->CreateBuffer(&desc, NULL, &cb);
		if (FAILED(hr)) {
			return JFG_ERROR;
		}

		renderer->dispatch_cbs[i] = cb;
	}

	// create instance buffer
	{
		ID3D11Buffer *instance_buffer = NULL;

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = INSTANCE_BUFFER_SIZE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = MAX_INSTANCE_WIDTH;

		hr = device->CreateBuffer(&desc, NULL, &instance_buffer);
		if (FAILED(hr)) {
			return JFG_ERROR;
		}

		renderer->instance_buffer = instance_buffer;
	}

	// create instance buffer SRV
	{
		ID3D11ShaderResourceView *srv = NULL;

		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements  = MAX_INSTANCES;

		hr = device->CreateShaderResourceView(renderer->instance_buffer,
		                                      &desc,
		                                      &srv);
		if (FAILED(hr)) {
			return JFG_ERROR;
		}

		renderer->instance_buffer_srv = srv;
	}

	// create blend state
	{
		ID3D11BlendState *bs = NULL;

		D3D11_BLEND_DESC desc = {};
		desc.AlphaToCoverageEnable = FALSE;
		desc.IndependentBlendEnable = FALSE;
		desc.RenderTarget[0].BlendEnable = TRUE;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = device->CreateBlendState(&desc, &bs);
		if (FAILED(hr)) {
			return JFG_ERROR;
		}
		renderer->blend_state = bs;
	}

	// create rasterizer state
	{
		ID3D11RasterizerState *rs = NULL;

		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.FrontCounterClockwise = FALSE;
		desc.DepthBias = 0;
		desc.DepthBiasClamp = 0.0f;
		desc.SlopeScaledDepthBias = 0.0f;
		desc.DepthClipEnable = FALSE;
		desc.ScissorEnable = FALSE;
		desc.MultisampleEnable = FALSE;
		desc.AntialiasedLineEnable = FALSE;

		hr = device->CreateRasterizerState(&desc, &rs);
		if (FAILED(hr)) {
			return JFG_ERROR;
		}

		renderer->rasterizer_state = rs;
	}

	// create sprite instance buffer
	{
		ID3D11Buffer *instance_buffer = NULL;

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = INSTANCE_BUFFER_SIZE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		// XXX -- don't know if this is necessary?
		desc.StructureByteStride = sizeof(Sprite_Instance);

		hr = device->CreateBuffer(&desc, NULL, &instance_buffer);
		if (FAILED(hr)) {
			jfg_set_error("Failed to create sprite instance buffer!");
			return JFG_ERROR;
		}

		renderer->sprite_instance_buffer = instance_buffer;
	}

	// create sprite instance buffer SRV
	/*
	{
		ID3D11ShaderResourceView *srv = NULL;

		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.FirstElement = 0;
		desc.Buffer.NumElements  = MAX_INSTANCES;

		hr = device->CreateShaderResourceView(renderer->sprite_instance_buffer, &desc, &srv);
		if (FAILED(hr)) {
			jfg_set_error("Failed to create sprite instance buffer srv!");
			return JFG_ERROR;
		}

		renderer->sprite_instance_buffer_srv = srv;
	}
	*/

	// create sprite input layout
	{
		ID3D11InputLayout *input_layout = NULL;

		D3D11_INPUT_ELEMENT_DESC input_element_descs[] = {
			{ "GLYPH_COORDS",  0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "OUTPUT_COORDS", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
			{ "COLOR",         0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
		};

		ID3DBlob *vs_code = renderer->vs_code[DX11_VS_SPRITE];
		hr = device->CreateInputLayout(input_element_descs, ARRAY_SIZE(input_element_descs), vs_code->GetBufferPointer(), vs_code->GetBufferSize(), &input_layout);
		if (FAILED(hr)) {
			jfg_set_error("Failed to create input layout for sprite renderer!");
			return JFG_ERROR;
		}

		renderer->sprite_input_layout = input_layout;
	}

	return JFG_SUCCESS;
}

bool set_screen_size(DX11_Renderer* renderer, v2_u32 screen_size)
{
	// Adjust backbuffer/swapchain
	renderer->back_buffer_rtv->Release();
	renderer->back_buffer->Release();
	HRESULT hr = renderer->swap_chain->ResizeBuffers(2, screen_size.w, screen_size.h,
		DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
	if (FAILED(hr)) {
		return false;
	}
	hr = renderer->swap_chain->GetBuffer(0, IID_PPV_ARGS(&renderer->back_buffer));
	if (FAILED(hr)) {
		return false;
	}

	D3D11_TEXTURE2D_DESC back_buffer_desc = {};
	renderer->back_buffer->GetDesc(&back_buffer_desc);

	D3D11_RENDER_TARGET_VIEW_DESC back_buffer_rtv_desc = {};
	back_buffer_rtv_desc.Format = back_buffer_desc.Format;
	back_buffer_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	back_buffer_rtv_desc.Texture2D.MipSlice = 0;

	hr = renderer->device->CreateRenderTargetView(renderer->back_buffer, &back_buffer_rtv_desc,
		&renderer->back_buffer_rtv);

	if (FAILED(hr)) {
		return false;
	}
	
	// Maybe adjust output target
	v2_u32 prev_max_screen_size = renderer->max_screen_size;
	if (screen_size.w < prev_max_screen_size.w && screen_size.h < prev_max_screen_size.h) {
		return true;
	}
	auto max_screen_size = max_v2_u32(renderer->max_screen_size, screen_size);

	if (!create_output_texture(renderer, max_screen_size)) {
		return false;
	}
	renderer->screen_size = screen_size;

	return true;
}

void free(DX11_Renderer* renderer)
{
	// TODO -- close DLL etc
}

bool check_errors(DX11_Renderer* renderer, u32 frame_number)
{
	char buffer[4096];
	Log *l = renderer->log;
	ID3D11InfoQueue *info_queue = renderer->info_queue;
	u64 num_messages = renderer->info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
	if (num_messages) {
		logf(l, "Frame %u: There are %llu D3D11 messages:", frame_number, num_messages);
		for (u64 i = 0; i < num_messages; ++i) {
			size_t message_len;
			HRESULT hr = info_queue->GetMessage(i, NULL, &message_len);
			ASSERT(message_len < ARRAY_SIZE(buffer));
			ASSERT(SUCCEEDED(hr));
			D3D11_MESSAGE *message = (D3D11_MESSAGE*)buffer;
			hr = info_queue->GetMessage(i, message, &message_len);
			ASSERT(SUCCEEDED(hr));
			log(l, message->pDescription);
		}
		info_queue->ClearStoredMessages();
		return true;
	}
	return false;
}