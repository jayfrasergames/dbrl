#include "debug_draw_world.h"

#include "jfg_math.h"

thread_local Debug_Draw_World *debug_draw_world_context = NULL;

void debug_draw_world_reset()
{
	ASSERT(debug_draw_world_context);
	debug_draw_world_context->reset();
}

void debug_draw_world_push_state()
{
	ASSERT(debug_draw_world_context);
	Debug_Draw_World_State state = {};
	state.triangle_len = debug_draw_world_context->triangles.len;
	state.lines_len = debug_draw_world_context->lines.len;
	state.color = debug_draw_world_context->current_color;
	debug_draw_world_context->states.push(state);
}

void debug_draw_world_restore_state()
{
	ASSERT(debug_draw_world_context);
	Debug_Draw_World_State state = debug_draw_world_context->states.peek();
	debug_draw_world_context->triangles.len = state.triangle_len;
	debug_draw_world_context->lines.len = state.lines_len;
	debug_draw_world_context->current_color = state.color;
}

void debug_draw_world_pop_state()
{
	ASSERT(debug_draw_world_context);
	debug_draw_world_context->states.pop();
	debug_draw_world_restore_state();
}

void debug_draw_world_set_color(v4 color)
{
	ASSERT(debug_draw_world_context);
	debug_draw_world_context->current_color = color;
}

void debug_draw_world_arrow(v2 start, v2 end, f32 margin)
{
	ASSERT(debug_draw_world_context);
	ASSERT((start.x != end.x) || (start.y != end.y));
	v2 dir = end - start;
	f32 angle = atan2f(dir.y, dir.x);
	m2 rot = m2::rotation(angle);

	v2 end_offset = end + 0.5f;
	v2 start_offset = start + 0.5f;

	v2 a = rot * v2(-0.125f + margin,  0.125f) + start_offset;
	v2 b = rot * v2(-0.125f + margin, -0.125f) + start_offset;

	v2 c = rot * v2(-0.25f - margin,  0.125f) + end_offset;
	v2 d = rot * v2(-0.25f - margin, -0.125f) + end_offset;

	v2 e = rot * v2(-0.25f - margin,  0.25f) + end_offset;
	v2 f = rot * v2(-0.25f - margin, -0.25f) + end_offset;
	v2 g = rot * v2(0.125f - margin,   0.0f) + end_offset;

	Debug_Draw_World_Triangle t = {};
	t.color = debug_draw_world_context->current_color;
	t.a = a; t.b = b; t.c = c;
	debug_draw_world_context->triangles.append(t);
	t.a = b; t.b = c; t.c = d;
	debug_draw_world_context->triangles.append(t);
	t.a = e; t.b = f; t.c = g;
	debug_draw_world_context->triangles.append(t);
}

void debug_draw_world_circle(v2 center, f32 radius)
{
	ASSERT(debug_draw_world_context);

	u32 num_points = 4;

	center = center + 0.5f;
	v2 v = v2(radius, 0.0f);

	m2 m = m2::rotation(2.0f * PI_F32 / (f32)(4 * num_points));

	Debug_Draw_World_Triangle t = {};
	t.color = debug_draw_world_context->current_color;
	for (u32 i = 0; i < 4 * num_points; ++i) {
		v2 w = m * v;
		t.a = center;
		t.b = center + v;
		t.c = center + w;
		debug_draw_world_context->triangles.append(t);
		v = w;
	}
}

void debug_draw_world_triangle(v2 a, v2 b, v2 c)
{
	ASSERT(debug_draw_world_context);

	Debug_Draw_World_Triangle t = {};
	t.a = a + 0.5f;
	t.b = b + 0.5f;
	t.c = c + 0.5f;
	t.color = debug_draw_world_context->current_color;
	debug_draw_world_context->triangles.append(t);
}

void debug_draw_world_sqaure(v2 center, f32 edge_length)
{
	ASSERT(debug_draw_world_context);

	f32 k = edge_length / 2.0f;
	Debug_Draw_World_Triangle t = {};
	t.a = center + 0.5f + v2(-k, -k);
	t.b = center + 0.5f + v2( k, -k);
	t.c = center + 0.5f + v2(-k,  k);
	t.color = debug_draw_world_context->current_color;
	debug_draw_world_context->triangles.append(t);

	t.a = center + 0.5f + v2(-k,  k);
	t.b = center + 0.5f + v2( k, -k);
	t.c = center + 0.5f + v2( k,  k);
	debug_draw_world_context->triangles.append(t);
}

void debug_draw_world_line(v2 start, v2 end)
{
	ASSERT(debug_draw_world_context);

	Debug_Draw_World_Line l = {};
	l.start = start + 0.5f;
	l.end = end + 0.5f;
	l.color = debug_draw_world_context->current_color;
	debug_draw_world_context->lines.append(l);
}