#include "stdafx.h"

#include <windows.h>

#include "draw.h"
#include "draw_dx11.h"

int main(int argc, char **argv)
{
	char base_path_buffer[4096] = {};
	GetFullPathName(argv[0], ARRAY_SIZE(base_path_buffer), base_path_buffer, NULL);
	for (u32 i = strlen(base_path_buffer); base_path_buffer[i] != '\\'; --i) {
		base_path_buffer[i] = 0;
	}
	SetCurrentDirectory(base_path_buffer);
	GetCurrentDirectory(ARRAY_SIZE(base_path_buffer), base_path_buffer);
	printf("%s\n", base_path_buffer);

	DX11_Renderer dx11_renderer = {};
	Render_Data *render_data = (Render_Data*)&dx11_renderer;

	// XXX -- get around asserts
	dx11_renderer.device = (ID3D11Device*)1;
	dx11_renderer.device_context = (ID3D11DeviceContext*)1;

	init(&dx11_renderer);
	// const Render_Functions *render_functions = &dx11_render_functions;
	// render_functions->reload_shaders(render_data);

	return EXIT_SUCCESS;
}