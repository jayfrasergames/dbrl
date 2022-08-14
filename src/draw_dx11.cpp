#include "draw_dx11.h"

#include "stdafx.h"
#include "jfg_d3d11.h"
#include <d3d11_1.h>
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

const wchar_t *RENDER_EVENT_NAMES[] = {
#define EVENT(name) L#name,
	RENDER_EVENTS
#undef EVENT
};

static HMODULE d3d_compiler_library = NULL;
const char* D3D_COMPILER_DLL_NAME = "D3DCompiler_47.dll";
const char* SHADER_DIR = "..\\assets\\shaders";

static DX11_Vertex_Shader vertex_shader_for_instance_buffer(Instance_Buffer_ID instance_buffer_id)
{
	switch (instance_buffer_id) {
	case INSTANCE_BUFFER_SPRITE:       return DX11_VS_SPRITE;
	case INSTANCE_BUFFER_TRIANGLE:     return DX11_VS_TRIANGLE;
	case INSTANCE_BUFFER_FOV_EDGE:     return DX11_VS_FOV_EDGE;
	case INSTANCE_BUFFER_FOV_FILL:     return DX11_VS_FOV_FILL;
	case INSTANCE_BUFFER_WORLD_SPRITE: return DX11_VS_SPRITE_SHEET_RENDER;
	case INSTANCE_BUFFER_WORLD_FONT:   return DX11_VS_SPRITE_SHEET_FONT;
	case INSTANCE_BUFFER_PARTICLES:    return DX11_VS_PARTICLES;
	}
	ASSERT(0);
	return (DX11_Vertex_Shader)-1;
}

static DXGI_FORMAT get_dxgi_format(Format format)
{
	switch (format) {
	case FORMAT_R32_F:          return DXGI_FORMAT_R32_FLOAT;
	case FORMAT_R32G32_F:       return DXGI_FORMAT_R32G32_FLOAT;
	case FORMAT_R32G32B32A32_F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case FORMAT_R8_U:           return DXGI_FORMAT_R8_UINT;
	case FORMAT_R32_U:          return DXGI_FORMAT_R32_UINT;
	case FORMAT_D16_UNORM:      return DXGI_FORMAT_D16_UNORM;
	}
	ASSERT(0);
	return DXGI_FORMAT_UNKNOWN;
}

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

static void set_name(ID3D11DeviceChild* object, const char* fmtstr, ...)
{
	char buffer[4096] = {};
	va_list args;
	va_start(args, fmtstr);
	vsnprintf(buffer, ARRAY_SIZE(buffer), fmtstr, args);
	va_end(args);

	size_t len = strlen(buffer);
	object->SetPrivateData(WKPDID_D3DDebugObjectName, len, buffer);
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
	for (u32 i = 0; i < NUM_SOURCE_TEXTURES; ++i) {
		SAFE_RELEASE(dx11_render->tex[i]);
		SAFE_RELEASE(dx11_render->srvs[i]);
	}

	for (u32 i = 0; i < NUM_SOURCE_TEXTURES; ++i) {
		auto tex = render->textures[i];

		D3D11_TEXTURE2D_DESC tex_desc = {};
		tex_desc.Width = tex->size.w;
		tex_desc.Height = tex->size.h;
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
		data_desc.pSysMem = tex->data;
		data_desc.SysMemPitch = tex->size.w * sizeof(u32);
		data_desc.SysMemSlicePitch = 0;

		ID3D11Texture2D *out_tex = NULL;
		HRESULT hr = dx11_render->device->CreateTexture2D(&tex_desc, &data_desc, &out_tex);
		if (FAILED(hr)) {
			// TODO -- cleanup!!
			// or do we just quietly fail...?
			return false;
		}

		set_name(out_tex, "TEXTURE(%s)", SOURCE_TEXTURE_FILENAMES[i]);

		ID3D11ShaderResourceView *srv = NULL;

		hr = dx11_render->device->CreateShaderResourceView(out_tex, NULL, &srv);
		if (FAILED(hr)) {
			return false;
		}

		set_name(srv, "SRV(%s)", SOURCE_TEXTURE_FILENAMES[i]);

		dx11_render->tex[i] = out_tex;
		dx11_render->srvs[i] = srv;
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

void begin_frame(DX11_Renderer* renderer, Render* render)
{
	ID3D11DeviceContext *dc = renderer->device_context;
	for (u32 i = 0; i < NUM_INSTANCE_BUFFERS; ++i) {
		auto dx_ib = renderer->instance_buffers[i];

		D3D11_MAPPED_SUBRESOURCE mapped_res = {};
		HRESULT hr = dc->Map(dx_ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
		ASSERT(SUCCEEDED(hr));

		auto abs_ib = &render->render_job_buffer.instance_buffers[i];
		abs_ib->base = mapped_res.pData;
		abs_ib->buffer_head = mapped_res.pData;
		abs_ib->cur_pos = 0;
	}
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

	ID3DUserDefinedAnnotation *uda = renderer->user_defined_annotation;

	uda->BeginEvent(L"Frame");

	HRESULT hr;
	u32 cur_cb = 0;
	ID3D11Buffer        **cbs = renderer->cbs;
	ID3D11Buffer        **dispatch_cbs = renderer->dispatch_cbs;
	ID3D11VertexShader  **vs  = renderer->vs;
	ID3D11PixelShader   **ps  = renderer->ps;
	ID3D11ComputeShader **cs  = renderer->cs;
	// ID3D11Texture2D     **tex = renderer->tex.items;
	ID3D11ShaderResourceView **srvs = renderer->srvs;

	ID3D11ShaderResourceView  **target_srvs = renderer->target_texture_srvs;
	ID3D11UnorderedAccessView **target_uavs = renderer->target_texture_uavs;
	ID3D11RenderTargetView    **target_rtvs = renderer->target_texture_rtvs;
	ID3D11DepthStencilView    **target_dsvs = renderer->target_texture_dsvs;

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

	for (u32 i = 0; i < ARRAY_SIZE(renderer->instance_buffers); ++i) {
		dc->Unmap(renderer->instance_buffers[i], 0);
	}

	uda->BeginEvent(L"Abstracted Renderer");

	ASSERT(render_jobs->event_stack.top == 0);

	for (u32 i = 0; i < render_jobs->jobs.len; ++i) {
		auto job = &render_jobs->jobs[i];
		dc->ClearState();
		switch (job->type) {

		case RENDER_JOB_NONE:
			ASSERT(0);
			break;

		case RENDER_JOB_TRIANGLES: {
			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_TRIANGLE]);
			u32 stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_TRIANGLE].element_size;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_TRIANGLE], &stride, &offset);

			ID3D11Buffer *cbs[] = { global_cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_TRIANGLE], NULL, 0);

			dc->RSSetViewports(1, &viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetShader(ps[DX11_PS_TRIANGLE], NULL, 0);

			ID3D11RenderTargetView *rtvs = { renderer->output_rtv };
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), &rtvs, NULL);
			dc->OMSetBlendState(renderer->blend_state, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(3, job->triangles.count, 0, job->triangles.start);
			break;
		}

		case RENDER_JOB_SPRITES: {
			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_SPRITE]);
			u32 stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_SPRITE].element_size;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_SPRITE], &stride, &offset);

			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};

			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->sprites.constants, sizeof(job->sprites.constants));
			dc->Unmap(cb, 0);

			ID3D11Buffer *cbs[] = { global_cb, cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_SPRITE], NULL, 0);

			dc->RSSetViewports(1, &viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetShaderResources(0, 1, &srvs[job->sprites.sprite_sheet_id]);
			dc->PSSetShader(ps[DX11_PS_SPRITE], NULL, 0);

			ID3D11RenderTargetView *rtvs = { renderer->output_rtv };
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), &rtvs, NULL);
			dc->OMSetBlendState(renderer->blend_state, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(6, job->sprites.count, 0, job->sprites.start);
			break;
		}

		case RENDER_JOB_CLEAR_UINT : {
			// XXX
			v2_u32 target_size = v2_u32(256*24, 256*24);

			dc->CSSetShader(cs[DX11_CS_FOV_CLEAR], NULL, 0);
			dc->CSSetUnorderedAccessViews(0, 1, &target_uavs[job->clear_uint.tex_id], NULL);
			dc->Dispatch(target_size.w / 8, target_size.h / 8, 1);

			break;
		}

		case RENDER_JOB_FOV: {

			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_FOV_FILL]);
			u32 stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_FOV_FILL].element_size;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_FOV_FILL], &stride, &offset);
		
			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};

			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->fov.constants, sizeof(job->fov.constants));
			dc->Unmap(cb, 0);

			ID3D11Buffer *cbs[] = { cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_FOV_FILL], NULL, 0);

			// XXX
			D3D11_VIEWPORT fov_viewport = {};
			fov_viewport.TopLeftX = 0;
			fov_viewport.TopLeftY = 0;
			fov_viewport.Width = 256*24;
			fov_viewport.Height = 256*24;
			fov_viewport.MinDepth = 0.0f;
			fov_viewport.MaxDepth = 1.0f;

			dc->RSSetViewports(1, &fov_viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->PSSetShader(ps[DX11_PS_FOV_FILL], NULL, 0);

			ID3D11RenderTargetView *rtvs = { renderer->target_texture_rtvs[job->fov.output_tex_id] };
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), &rtvs, NULL);
			dc->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(6, job->fov.fill_count, 0, job->fov.fill_start);

			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_FOV_EDGE]);
			stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_FOV_EDGE].element_size;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_FOV_EDGE], &stride, &offset);

			dc->VSSetShader(vs[DX11_VS_FOV_EDGE], NULL, 0);

			dc->PSSetShaderResources(0, 1, &srvs[SOURCE_TEXTURE_EDGES]);
			dc->PSSetShader(ps[DX11_PS_FOV_EDGE], NULL, 0);

			dc->DrawInstanced(6, job->fov.edge_count, 0, job->fov.edge_start);

			break;
		}

		case RENDER_JOB_WORLD_SPRITE: {

			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_WORLD_SPRITE]);
			u32 stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_WORLD_SPRITE].element_size;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_WORLD_SPRITE], &stride, &offset);
		
			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};

			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->world_sprite.constants, sizeof(job->world_sprite.constants));
			dc->Unmap(cb, 0);

			ID3D11Buffer *cbs[] = { cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_SPRITE_SHEET_RENDER], NULL, 0);

			// XXX
			D3D11_VIEWPORT fov_viewport = {};
			fov_viewport.TopLeftX = 0;
			fov_viewport.TopLeftY = 0;
			fov_viewport.Width = 256*24;
			fov_viewport.Height = 256*24;
			fov_viewport.MinDepth = 0.0f;
			fov_viewport.MaxDepth = 1.0f;

			dc->RSSetViewports(1, &fov_viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetShaderResources(0, 1, &srvs[job->world_sprite.sprite_tex_id]);
			dc->PSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->PSSetShader(ps[DX11_PS_SPRITE_SHEET_RENDER], NULL, 0);

			ID3D11RenderTargetView *rtvs[] = {
				target_rtvs[job->world_sprite.output_tex_id],
				target_rtvs[job->world_sprite.output_sprite_id_tex_id],
			};
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, target_dsvs[job->world_sprite.depth_tex_id]);
			dc->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);
			dc->OMSetDepthStencilState(renderer->depth_stencil_state, 0);

			dc->DrawInstanced(6, job->world_sprite.count, 0, job->world_sprite.start);

			break;
		}

		case RENDER_JOB_WORLD_FONT: {

			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_WORLD_FONT]);
			u32 stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_WORLD_FONT].element_size;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_WORLD_FONT], &stride, &offset);
		
			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};

			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->world_font.constants, sizeof(job->world_font.constants));
			dc->Unmap(cb, 0);

			ID3D11Buffer *cbs[] = { cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_SPRITE_SHEET_FONT], NULL, 0);

			// XXX
			D3D11_VIEWPORT fov_viewport = {};
			fov_viewport.TopLeftX = 0;
			fov_viewport.TopLeftY = 0;
			fov_viewport.Width = 256*24;
			fov_viewport.Height = 256*24;
			fov_viewport.MinDepth = 0.0f;
			fov_viewport.MaxDepth = 1.0f;

			dc->RSSetViewports(1, &fov_viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetShaderResources(0, 1, &srvs[job->world_font.font_tex_id]);
			dc->PSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->PSSetShader(ps[DX11_PS_SPRITE_SHEET_FONT], NULL, 0);

			ID3D11RenderTargetView *rtvs[] = {
				target_rtvs[job->world_font.output_tex_id],
			};
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
			dc->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(6, job->world_font.count, 0, job->world_font.start);

			break;
		}

		case RENDER_JOB_WORLD_PARTICLES: {

			dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
			dc->IASetInputLayout(renderer->input_layouts[INSTANCE_BUFFER_PARTICLES]);
			u32 stride = INSTANCE_BUFFER_METADATA[INSTANCE_BUFFER_PARTICLES].element_size;
			u32 offset = 0;
			dc->IASetVertexBuffers(0, 1, &renderer->instance_buffers[INSTANCE_BUFFER_PARTICLES], &stride, &offset);
		
			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};

			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &job->world_particles.constants, sizeof(job->world_particles.constants));
			dc->Unmap(cb, 0);

			ID3D11Buffer *cbs[] = { cb };
			dc->VSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->VSSetShader(vs[DX11_VS_PARTICLES], NULL, 0);

			// XXX
			D3D11_VIEWPORT fov_viewport = {};
			fov_viewport.TopLeftX = 0;
			fov_viewport.TopLeftY = 0;
			fov_viewport.Width = 256*24;
			fov_viewport.Height = 256*24;
			fov_viewport.MinDepth = 0.0f;
			fov_viewport.MaxDepth = 1.0f;

			dc->RSSetViewports(1, &fov_viewport);
			dc->RSSetState(renderer->rasterizer_state);

			dc->PSSetConstantBuffers(0, ARRAY_SIZE(cbs), cbs);
			dc->PSSetShader(ps[DX11_PS_PARTICLES], NULL, 0);

			ID3D11RenderTargetView *rtvs[] = {
				target_rtvs[job->world_particles.output_tex_id],
			};
			dc->OMSetRenderTargets(ARRAY_SIZE(rtvs), rtvs, NULL);
			dc->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);

			dc->DrawInstanced(1, job->world_particles.count, 0, job->world_particles.start);

			break;
		}

		case RENDER_JOB_CLEAR_RTV: {

			v4 c = job->clear_rtv.clear_value;
			FLOAT clear_value[] = { c.r, c.g, c.b, c.a };
			dc->ClearRenderTargetView(renderer->target_texture_rtvs[job->clear_rtv.tex_id], clear_value);

			break;
		}

		case RENDER_JOB_CLEAR_DEPTH: {

			dc->ClearDepthStencilView(target_dsvs[job->clear_depth.tex_id], D3D11_CLEAR_DEPTH, job->clear_depth.value, 0);

			break;
		}

		case RENDER_JOB_BEGIN_EVENT: {
			uda->BeginEvent(RENDER_EVENT_NAMES[job->begin_event.event]);
			break;
		}

		case RENDER_JOB_END_EVENT: {
			uda->EndEvent();
			break;
		}

		case RENDER_JOB_FOV_COMPOSITE: {

			dc->CSSetShader(cs[DX11_CS_FOV_COMPOSITE], NULL, 0);

			ID3D11ShaderResourceView *srvs[] = {
				target_srvs[job->fov_composite.fov_id],
				target_srvs[job->fov_composite.world_static_id],
				target_srvs[job->fov_composite.world_dynamic_id],
			};
			dc->CSSetShaderResources(0, ARRAY_SIZE(srvs), srvs);
			ID3D11UnorderedAccessView *uavs[] = {
				target_uavs[job->fov_composite.output_tex_id],
			};
			dc->CSSetUnorderedAccessViews(0, ARRAY_SIZE(uavs), uavs, NULL);

			dc->Dispatch(256*24 / 8, 256*24 / 8, 1);

			break;
		}

		case RENDER_JOB_WORLD_HIGHLIGHT_SPRITE_ID: {

			Sprite_Sheet_Highlight_Constant_Buffer constants = {};
			constants.sprite_id = job->world_highlight.sprite_id;
			constants.highlight_color = job->world_highlight.color;
	
			ASSERT(cur_cb < MAX_CONSTANT_BUFFERS);
			D3D11_MAPPED_SUBRESOURCE mapped_res = {};
			ID3D11Buffer *cb = cbs[cur_cb++];
			hr = dc->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);
			ASSERT(SUCCEEDED(hr));
			memcpy(mapped_res.pData, &constants, sizeof(constants));
			dc->Unmap(cb, 0);

			dc->CSSetShader(cs[DX11_CS_SPRITE_SHEET_HIGHLIGHT], NULL, 0);

			dc->CSSetConstantBuffers(0, 1, &cb);
			dc->CSSetShaderResources(0, 1, &target_srvs[job->world_highlight.sprite_id_tex_id]);
			dc->CSSetUnorderedAccessViews(0, 1, &target_uavs[job->world_highlight.output_tex_id], NULL);

			dc->Dispatch(256*24 / 8, 256*24 / 8, 1);

			break;
		}

		case RENDER_JOB_XXX_FLUSH_OLD_RENDERER: {
			uda->BeginEvent(L"Old renderer (compositing)");

			v2 world_tl = screen_pos_to_world_pos(&draw->camera,
							screen_size,
							{ 0, 0 });
			v2 world_br = screen_pos_to_world_pos(&draw->camera,
							screen_size,
							screen_size);
			v2 input_size = world_br - world_tl;
			pixel_art_upsampler_d3d11_draw(&renderer->pixel_art_upsampler,
						dc,
						target_srvs[TARGET_TEXTURE_WORLD_COMPOSITE],
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


			dc->ClearState();

			uda->EndEvent(); // Old renderer (compositing)

			break;
		}

		default:
			ASSERT(0);
			break;

		}
	}

	dc->ClearState();

	uda->EndEvent(); // Abstracted renderer
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dc->RSSetViewports(1, &viewport);

	dc->VSSetShader(renderer->vs[DX11_VS_PASS_THROUGH], NULL, 0);
	dc->PSSetShader(renderer->ps[DX11_PS_PASS_THROUGH], NULL, 0);
	dc->OMSetRenderTargets(1, &renderer->back_buffer_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &renderer->output_srv);
	dc->Draw(6, 0);

	uda->EndEvent(); // Frame

	renderer->swap_chain->Present(1, 0);
}

static bool program_d3d11_init(Draw* draw, ID3D11Device* device)
{
	if (!card_render_d3d11_init(&draw->card_render, device)) {
		goto error_init_card_render;
	}

	if (!debug_line_d3d11_init(&draw->card_debug_line, device)) {
		goto error_init_debug_line;
	}

	if (!debug_draw_world_d3d11_init(&draw->debug_draw_world, device)) {
		goto error_init_debug_draw_world;
	}

	return true;

	debug_draw_world_d3d11_free(&draw->debug_draw_world);
error_init_debug_draw_world:
	debug_line_d3d11_free(&draw->card_debug_line);
error_init_debug_line:
	card_render_d3d11_free(&draw->card_render);
error_init_card_render:
	return false;
}

static bool create_output_texture(DX11_Renderer* renderer, v2_u32 size)
{
	HRESULT hr;

	ID3D11Texture2D *output_texture;

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
	if (FAILED(hr)) {
		goto error_init_output_texture;
	}
	set_name(output_texture, "OUTPUT_TEXTURE");

	ID3D11UnorderedAccessView *output_uav;
	hr = renderer->device->CreateUnorderedAccessView(output_texture, NULL, &output_uav);
	if (FAILED(hr)) {
		goto error_init_output_uav;
	}
	set_name(output_uav, "OUTPUT_TEXTURE_UAV");

	ID3D11RenderTargetView *output_rtv;
	hr = renderer->device->CreateRenderTargetView(output_texture, NULL, &output_rtv);
	if (FAILED(hr)) {
		goto error_init_output_rtv;
	}
	set_name(output_rtv, "OUTPUT_TEXTURE_RTV");

	ID3D11ShaderResourceView *output_srv;
	hr = renderer->device->CreateShaderResourceView(output_texture, NULL, &output_srv);
	if (FAILED(hr)) {
		goto error_init_output_srv;
	}
	set_name(output_srv, "OUTPUT_TEXTURE_SRV");

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

JFG_Error init(DX11_Renderer* renderer, Render* abstract_renderer, Log* log, Draw* draw, HWND window)
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

	for (u32 i = 0; i < ARRAY_SIZE(renderer->instance_buffers); ++i) {
		ID3D11Buffer *instance_buffer = NULL;

		auto ib_metadata = &INSTANCE_BUFFER_METADATA[i];

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = ib_metadata->size;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		hr = device->CreateBuffer(&desc, NULL, &instance_buffer);
		if (FAILED(hr)) {
			jfg_set_error("Failed to create sprite instance buffer!");
			return JFG_ERROR;
		}
		set_name(instance_buffer, ib_metadata->name);

		renderer->instance_buffers[i] = instance_buffer;
	}

	for (u32 i = 0; i < ARRAY_SIZE(renderer->input_layouts); ++i) {
		ID3D11InputLayout *input_layout = NULL;

		auto ib = (Instance_Buffer_ID)i;
		auto ib_metadata = &INSTANCE_BUFFER_METADATA[i];
		auto vs = vertex_shader_for_instance_buffer(ib);

		D3D11_INPUT_ELEMENT_DESC input_elem_descs[NUM_INPUT_ELEMS] = {};

		u32 input_elem_start_index = NUM_INPUT_ELEMS;
		u32 num_input_elems = 0;
		for (u32 i = 0; i < ARRAY_SIZE(INSTANCE_BUFFER_INPUT_ELEM_METADATA); ++i) {
			auto ie_metadata = &INSTANCE_BUFFER_INPUT_ELEM_METADATA[i];
			if (ie_metadata->instance_buffer_id == ib) {
				auto desc = &input_elem_descs[num_input_elems++];
				desc->SemanticName = ie_metadata->semantic_name;
				desc->SemanticIndex = 0;
				desc->Format = get_dxgi_format(ie_metadata->format);
				desc->InputSlot = 0;
				desc->AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
				desc->InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
				desc->InstanceDataStepRate = 1;
			}
		}

		auto vs_code = renderer->vs_code[vs];
		hr = device->CreateInputLayout(input_elem_descs,
		                               num_input_elems,
		                               vs_code->GetBufferPointer(),
		                               vs_code->GetBufferSize(),
		                               &input_layout);
		if (FAILED(hr)) {
			check_errors(renderer, 0);
			jfg_set_error("Failed to create input layout!");
			return JFG_ERROR;
		}
		set_name(input_layout, ib_metadata->name);

		renderer->input_layouts[i] = input_layout;
	}

	// create target textures
	for (u32 i = 0; i < NUM_TARGET_TEXTURES; ++i) {
		ID3D11Texture2D           *tex = NULL;
		ID3D11ShaderResourceView  *srv = NULL;
		ID3D11UnorderedAccessView *uav = NULL;
		ID3D11RenderTargetView    *rtv = NULL;
		ID3D11DepthStencilView    *dsv = NULL;

		auto tex_metadata = &TARGET_TEXTURE_METADATA[i];

		UINT bind_flags = 0;
		if (tex_metadata->bind_flags & BIND_SRV) { bind_flags |= D3D11_BIND_SHADER_RESOURCE; }
		if (tex_metadata->bind_flags & BIND_UAV) { bind_flags |= D3D11_BIND_UNORDERED_ACCESS; }
		if (tex_metadata->bind_flags & BIND_RTV) { bind_flags |= D3D11_BIND_RENDER_TARGET; }
		if (tex_metadata->bind_flags & BIND_DSV) { bind_flags |= D3D11_BIND_DEPTH_STENCIL; }
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width  = tex_metadata->size.w;
		desc.Height = tex_metadata->size.h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = get_dxgi_format(tex_metadata->format);
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = bind_flags;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		hr = device->CreateTexture2D(&desc, NULL, &tex);
		if (FAILED(hr)) {
			jfg_set_error("Failed to create target texture!");
			return JFG_ERROR;
		}
		set_name(tex, tex_metadata->name);

		if (tex_metadata->bind_flags & BIND_SRV) {
			hr = device->CreateShaderResourceView(tex, NULL, &srv);
			if (FAILED(hr)) {
				jfg_set_error("Failed to create SRV!");
				return JFG_ERROR;
			}
			set_name(srv, "%s_SRV", tex_metadata->name);
		}

		if (tex_metadata->bind_flags & BIND_UAV) {
			hr = device->CreateUnorderedAccessView(tex, NULL, &uav);
			if (FAILED(hr)) {
				jfg_set_error("Failed to create UAV!");
				return JFG_ERROR;
			set_name(uav, "%s_UAV", tex_metadata->name);
			}
		}

		if (tex_metadata->bind_flags & BIND_RTV) {
			hr = device->CreateRenderTargetView(tex, NULL, &rtv);
			if (FAILED(hr)) {
				jfg_set_error("Failed to create RTV!");
				return JFG_ERROR;
			}
			set_name(rtv, "%s_RTV", tex_metadata->name);
		}

		if (tex_metadata->bind_flags & BIND_DSV) {
			hr = device->CreateDepthStencilView(tex, NULL, &dsv);
			if (FAILED(hr)) {
				jfg_set_error("Failed to create DSV!");
				return JFG_ERROR;
			}
			set_name(dsv, "%s_DSV", tex_metadata->name);
		}

		renderer->target_textures[i] = tex;
		renderer->target_texture_srvs[i] = srv;
		renderer->target_texture_uavs[i] = uav;
		renderer->target_texture_rtvs[i] = rtv;
		renderer->target_texture_dsvs[i] = dsv;
	}

	// depth stencil state
	{
		ID3D11DepthStencilState *depth_stencil_state = NULL;

		D3D11_DEPTH_STENCIL_DESC desc = {};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_GREATER;
		desc.StencilEnable = FALSE;

		hr = device->CreateDepthStencilState(&desc, &depth_stencil_state);
		if (FAILED(hr)) {
			jfg_set_error("Failed to create depth stencil state!");
			return JFG_ERROR;
		}

		renderer->depth_stencil_state = depth_stencil_state;
	}

	// init user defined annotations
	{
		ID3DUserDefinedAnnotation *uda = NULL;

		auto dc = renderer->device_context;
		dc->QueryInterface(IID_PPV_ARGS(&uda));

		if (uda == NULL) {
			jfg_set_error("Failed to create user defined annotations interface!");
			return JFG_ERROR;
		}

		renderer->user_defined_annotation = uda;
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
		if (frame_number) {
			logf(l, "Frame %u: There are %llu D3D11 messages:", frame_number, num_messages);
		} else {
			logf(l, "There are %llu D3D11 messages:", num_messages);
		}
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