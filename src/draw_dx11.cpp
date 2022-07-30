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

struct Shader_Metadata
{
	const char* name;
	const char* filename;
	const char* entry_point;
};

const Shader_Metadata PS_SHADER_METADATA[] = {
#define DX11_PS(name, filename, entry_point) { #name, filename, entry_point },
	DX11_PIXEL_SHADERS
#undef DX11_PS
};

bool reload_shaders(Render_Data* data)
{
	auto renderer = (DX11_Renderer*)data;

	for (u32 i = 0; i < ARRAY_SIZE(PS_SHADER_METADATA); ++i) {
		auto ps_metadata = &PS_SHADER_METADATA[i];
		printf("Loading %s (\"%s\" -- \"%s\")\n",
		       ps_metadata->name,
		       ps_metadata->filename,
		       ps_metadata->entry_point);

		auto filename = fmt("%s\\%s", SHADER_DIR, ps_metadata->filename);
		printf("Compiling \"%s\"\n", filename);

		ID3DBlob *code = NULL, *error_messages = NULL;

		wchar_t filename_wide[4096] = {};
		mbstowcs(filename_wide, filename, ARRAY_SIZE(filename_wide));

		HRESULT hr = D3DCompileFromFile(filename_wide,
		                                NULL,
		                                D3D_COMPILE_STANDARD_FILE_INCLUDE,
		                                ps_metadata->entry_point,
		                                "ps_5_0",
		                                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS,
		                                0,
		                                &code,
		                                &error_messages);

		if (FAILED(hr)) {
			// XXX -- check if error_messages == NULL and if so make an error message based on the HRESULT
			printf("Failed to compile shader \"%s\"!\nErrors:\n%s\n",
			       ps_metadata->name,
			       (char*)error_messages->GetBufferPointer());
			return false;
		}
	}

	return true;
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
	reload_shaders
};