#include "jfg_d3d11.h"

#define D3D11_FUNCTION(return_type, name, ...) return_type (WINAPI *name)(__VA_ARGS__) = NULL;
D3D11_FUNCTIONS
#undef D3D11_FUNCTION

bool d3d11_try_load()
{
	HMODULE d3d11_library = LoadLibraryA("d3d11.dll");
	if (!d3d11_library) {
		return false;
	}

#define D3D11_FUNCTION(return_type, name, ...) \
		name = (return_type (WINAPI *)(__VA_ARGS__))GetProcAddress(d3d11_library, #name);
	D3D11_FUNCTIONS
#undef D3D11_FUNCTION

	return true;
}
