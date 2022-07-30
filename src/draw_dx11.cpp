#include "draw_dx11.h"

#include "stdafx.h"
#include "jfg_d3d11.h"
#include "draw.h"

#define D3DCompileFromFile _D3DCompileFromFile
#include "d3dcompiler.h"
#undef D3DCompileFromFile

#define D3D_COMPILE_FUNCS \
	D3D_COMPILE_FUNC(HRESULT, D3DCompileFromFile, LPCWSTR pFileName, const D3D_SHADER_MACRO *pDefines, ID3DInclude *pInclude, LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs)

#define D3D_COMPILE_FUNC(return_type, name, ...) return_type (WINAPI *name)(__VA_ARGS__) = NULL;
D3D_COMPILE_FUNCS
#undef D3D_COMPILE_FUNC

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

static bool reload_shaders(Render_Data* data)
{
	auto renderer = (DX11_Renderer*)data;

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
			break;
		}
		}

	}

	return true;
}

static void draw(Render_Data* data, Draw* draw)
{
	/*
	auto dx11_renderer = (DX11_Renderer*)data;

	ID3D11DeviceContext* dc = dx11_renderer->device_context;
	ID3D11RenderTargetView* output_rtv;

	v2_u32 screen_size_u32 = (v2_u32)program->screen_size;

	f32 clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	dc->ClearRenderTargetView(program->d3d11.output_rtv, clear_color);

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
	                           (v2_u32)program->draw.renderer.size);

	f32 zoom = program->draw.camera.zoom;
	v2 world_tl = screen_pos_to_world_pos(&draw->camera,
	                                      screen_size_u32,
	                                      { 0, 0 });
	v2 world_br = screen_pos_to_world_pos(&draw->camera,
	                                      screen_size_u32,
	                                      screen_size_u32);
	v2 input_size = world_br - world_tl;
	pixel_art_upsampler_d3d11_draw(&pixel_art_upsampler,
	                               dc,
	                               draw->renderer.d3d11.output_srv,
	                               d3d11.output_uav,
	                               input_size,
	                               world_tl,
	                               (v2)screen_size_u32,
	                               { 0.0f, 0.0f });

	card_render_d3d11_draw(&draw->card_render,
	                       dc,
	                       screen_size_u32,
	                       d3d11.output_rtv);
	// debug_line_d3d11_draw(&program->draw.card_debug_line, dc, program->d3d11.output_rtv);

	v2 debug_zoom = (v2)(world_br - world_tl) / 24.0f;
	debug_draw_world_d3d11_draw(&program->debug_draw_world,
	                            dc,
	                            program->d3d11.output_rtv,
	                            draw->camera.world_center + draw->camera.offset,
	                            debug_zoom);

	imgui_d3d11_draw(&program->imgui, dc, program->d3d11.output_rtv, screen_size_u32);

	dc->VSSetShader(program->d3d11.output_vs, NULL, 0);
	dc->PSSetShader(program->d3d11.output_ps, NULL, 0);
	dc->OMSetRenderTargets(1, &output_rtv, NULL);
	dc->PSSetShaderResources(0, 1, &program->d3d11.output_srv);
	dc->Draw(6, 0);
	*/
}

bool init(DX11_Renderer* renderer)
{
	if (!d3d11_try_load()) {
		return false;
	}

	d3d_compiler_library = LoadLibraryA("D3DCompiler_47.dll");
	if (!d3d_compiler_library) {
		return false;
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
		return false;
	}

	if (!reload_shaders((Render_Data*)renderer)) {
		return false;
	}

	return true;
}

void close(DX11_Renderer* data)
{
	// TODO -- close DLL etc
}

const Render_Functions dx11_render_functions = {
	reload_shaders,
	draw,
};