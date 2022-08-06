#ifndef JFG_D3D11_H
#define JFG_D3D11_H

// TODO -- want to get rid of the necessity to use the actual d3d11.h header
// instead load the .dll ourselves and provide a suitable failure if it isn't
// found.

// XXX - this hack seems nice for now

#define D3D11CreateDevice _D3D11CreateDevice

// XXX -- might need to move this to an implementation file to avoid multiple pieces of code
// including d3d11.h defining symbols for GUIDs
#include <initguid.h>
#include <d3d11.h>

#undef D3D11CreateDevice

#define D3D11_FUNCTIONS \
	D3D11_FUNCTION(HRESULT, D3D11CreateDevice, IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)

#define D3D11_FUNCTION(return_type, name, ...) extern return_type (WINAPI *name)(__VA_ARGS__);
D3D11_FUNCTIONS
#undef D3D11_FUNCTION

bool d3d11_try_load();

#endif
