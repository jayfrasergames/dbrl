#pragma once

#include "prelude.h"
#include "containers.hpp"

#include "debug_draw_world_gpu_data_types.h"

#define DEBUG_DRAW_MAX_TRIANGLES (4 * 1024 * 1024)
#define DEBUG_DRAW_MAX_LINES     4096
#define DEBUG_DRAW_MAX_STATES    32

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
void debug_draw_world_arrow(v2 start, v2 end, f32 margin = 0.0f);
void debug_draw_world_circle(v2 center, f32 radius);
void debug_draw_world_triangle(v2 a, v2 b, v2 c);
void debug_draw_world_sqaure(v2 center, f32 edge_length);
void debug_draw_world_line(v2 start, v2 end);