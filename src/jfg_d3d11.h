#ifndef JFG_D3D11_H
#define JFG_D3D11_H

// TODO -- want to get rid of the necessity to use the actual d3d11.h header
// instead load the .dll ourselves and provide a suitable failure if it isn't
// found.

// XXX - this hack seems nice for now

#define D3D11CreateDevice _D3D11CreateDevice

#include <d3d11.h>

#undef D3D11CreateDevice

#define D3D11_FUNCTIONS \
	D3D11_FUNCTION(HRESULT, D3D11CreateDevice, IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)

#define D3D11_FUNCTION(return_type, name, ...) extern return_type (WINAPI *name)(__VA_ARGS__);
D3D11_FUNCTIONS
#undef D3D11_FUNCTION

u8 d3d11_try_load();

#ifndef JFG_HEADER_ONLY

#define D3D11_FUNCTION(return_type, name, ...) return_type (WINAPI *name)(__VA_ARGS__) = NULL;
D3D11_FUNCTIONS
#undef D3D11_FUNCTION

u8 d3d11_try_load()
{
	HMODULE d3d11_library = LoadLibraryA("d3d11.dll");
	if (!d3d11_library) {
		return 0;
	}
#define D3D11_FUNCTION(return_type, name, ...) \
		name = (return_type (WINAPI *)(__VA_ARGS__))GetProcAddress(d3d11_library, #name);
	D3D11_FUNCTIONS
#undef D3D11_FUNCTION
	return 1;
}

#endif

#endif
