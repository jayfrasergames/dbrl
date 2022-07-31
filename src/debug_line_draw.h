#ifndef JFG_DEBUG_LINE_DRAW_H
#define JFG_DEBUG_LINE_DRAW_H

#include "stdafx.h"

#ifdef JFG_D3D11_H

#ifndef DEBUG_LINE_INCLUDE_GFX
#define DEBUG_LINE_INCLUDE_GFX
#endif

struct Debug_Line_D3D11
{
	ID3D11VertexShader       *vertex_shader;
	ID3D11PixelShader        *pixel_shader;
	ID3D11Buffer             *constants;
	ID3D11Buffer             *instances;
	ID3D11ShaderResourceView *instances_srv;
};

#endif

#include "debug_line_gpu_data_types.h"

#define DEBUG_LINE_MAX_LINES 1024

struct Debug_Line
{
	u32                        num_lines;
	Debug_Line_Instance        instances[DEBUG_LINE_MAX_LINES];
	Debug_Line_Constant_Buffer constants;

#ifdef DEBUG_LINE_INCLUDE_GFX
	union {
	#ifdef JFG_D3D11_H
		Debug_Line_D3D11 d3d11;
	#endif
	};
#endif
};

void debug_line_reset(Debug_Line* debug_line);
void debug_line_add_instance(Debug_Line* debug_line, Debug_Line_Instance instance);

#ifndef JFG_HEADER_ONLY
#endif

#ifdef JFG_D3D11_H

u8 debug_line_d3d11_init(Debug_Line* debug_line, ID3D11Device* device);
void debug_line_d3d11_free(Debug_Line* debug_line);
void debug_line_d3d11_draw(Debug_Line*             debug_line,
                           ID3D11DeviceContext*    dc,
                           ID3D11RenderTargetView* output_rtv);

#ifndef JFG_HEADER_ONLY
#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H

#endif
