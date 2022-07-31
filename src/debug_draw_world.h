#ifndef DEBUG_DRAW_WORLD_H
#define DEBUG_DRAW_WORLD_H

#include "prelude.h"
#include "containers.hpp"

#include "debug_draw_world_gpu_data_types.h"

#define DEBUG_DRAW_MAX_TRIANGLES (4 * 1024 * 1024)
#define DEBUG_DRAW_MAX_LINES     4096
#define DEBUG_DRAW_MAX_STATES    32

#ifdef JFG_D3D11_H

#ifndef INCLUDE_GFX_API
#define INCLUDE_GFX_API
#endif

struct Debug_Draw_World_D3D11
{
	ID3D11Buffer             *triangle_constant_buffer;
	ID3D11Buffer             *triangle_instance_buffer;
	ID3D11ShaderResourceView *triangle_instance_buffer_srv;
	ID3D11VertexShader       *triangle_vertex_shader;
	ID3D11PixelShader        *triangle_pixel_shader;
	ID3D11BlendState         *triangle_blend_state;
	ID3D11Buffer             *line_instance_buffer;
	ID3D11ShaderResourceView *line_instance_buffer_srv;
	ID3D11VertexShader       *line_vertex_shader;
	ID3D11PixelShader        *line_pixel_shader;
};

#endif // JFG_D3D11_H

struct Debug_Draw_World;
extern thread_local Debug_Draw_World *debug_draw_world_context;

struct Debug_Draw_World_State
{
	u32 triangle_len;
	u32 lines_len;
	v4 color;
};

struct Debug_Draw_World
{
	Max_Length_Array<Debug_Draw_World_Triangle, DEBUG_DRAW_MAX_TRIANGLES> triangles;
	Max_Length_Array<Debug_Draw_World_Line, DEBUG_DRAW_MAX_LINES> lines;
	v4 current_color;
	Stack<Debug_Draw_World_State, DEBUG_DRAW_MAX_STATES> states;

#ifdef INCLUDE_GFX_API
	union {
	#ifdef JFG_D3D11_H
		Debug_Draw_World_D3D11 d3d11;
	#endif
	};
#endif

	void set_current() { debug_draw_world_context = this; }
	void reset()
	{
		triangles.reset();
		lines.reset();
		states.reset();
		Debug_Draw_World_State state = {};
		state.triangle_len = 0;
		state.lines_len = 0;
		state.color = v4(1.0f, 0.0f, 0.0f, 1.0f);
		states.push(state);
	}
};

void debug_draw_world_reset();
void debug_draw_world_push_state();
void debug_draw_world_restore_state();
void debug_draw_world_pop_state();
void debug_draw_world_set_color(v4 color);
void debug_draw_world_arrow(v2 start, v2 end);
void debug_draw_world_circle(v2 center, f32 radius);
void debug_draw_world_triangle(v2 a, v2 b, v2 c);
void debug_draw_world_sqaure(v2 center, f32 edge_length);
void debug_draw_world_line(v2 start, v2 end);

#ifdef JFG_D3D11_H

u8 debug_draw_world_d3d11_init(Debug_Draw_World* context, ID3D11Device* device);
void debug_draw_world_d3d11_free(Debug_Draw_World* context);
void debug_draw_world_d3d11_draw(Debug_Draw_World*       context,
                                 ID3D11DeviceContext*    dc,
                                 ID3D11RenderTargetView* output_rtv,
                                 v2                      world_center,
                                 v2                      zoom);

#ifndef JFG_HEADER_ONLY
#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H


#endif
