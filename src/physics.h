#ifndef PHYSICS_H
#define PHYSICS_H

#include "jfg/prelude.h"
#include "jfg/containers.hpp"
#include "jfg/jfg_math.h"

#include "types.h"
#include "debug_draw_world.h"

#define PHYSICS_MAX_STATIC_LINES   1024
#define PHYSICS_MAX_STATIC_CIRCLES 1024
#define PHYSICS_MAX_LINEAR_CIRCLES 1024
#define PHYSICS_MAX_COLLISIONS     1024

struct Physics_Object_Meta_Data
{
	Entity_ID owner_id;
	u32 collision_mask;
	u32 collides_with_mask;
};

struct Physics_Static_Line : Physics_Object_Meta_Data
{
	v2 start;
	v2 end;
};

struct Physics_Static_Circle : Physics_Object_Meta_Data
{
	v2 pos;
	f32 radius;
};

struct Physics_Linear_Circle : Physics_Object_Meta_Data
{
	v2 start;
	v2 velocity;
	f32 radius;
	f32 start_time;
	f32 duration;
};

struct Physics_Interval
{
	u32 object_index;
	f32 start;
	f32 end;
};

enum Physics_Collision_Type
{
	PHYSICS_COLLISION_NONE,
	PHYSICS_COLLISION_BEGIN_PENETRATION,
	PHYSICS_COLLISION_END_PENETRATION,
};

enum Physics_Collision_Objects_Type
{
	PHYSICS_COLLISION_STATIC_LINE_LINEAR_CIRCLE,
	PHYSICS_COLLISION_STATIC_CIRCLE_LINEAR_CIRCLE,
	PHYSICS_COLLISION_LINEAR_CIRCLE_LINEAR_CIRCLE,
};

struct Physics_Collision
{
	Physics_Collision_Type type;
	Physics_Collision_Objects_Type objects_type;
	f32 time;
	union {
		struct {
			Physics_Static_Line   *static_line;
			Physics_Linear_Circle *linear_circle;
		} static_line_linear_circle;
		struct {
			Physics_Static_Circle *static_circle;
			Physics_Linear_Circle *linear_circle;
		} static_circle_linear_circle;
		struct {
			Physics_Linear_Circle *linear_circle_1;
			Physics_Linear_Circle *linear_circle_2;
		} linear_circle_linear_circle;
	};
};

struct Physics_Context
{
	Max_Length_Array<Physics_Static_Line, PHYSICS_MAX_STATIC_LINES> static_lines;
	Max_Length_Array<Physics_Interval,    PHYSICS_MAX_STATIC_LINES> static_line_x_intervals;
	Max_Length_Array<Physics_Interval,    PHYSICS_MAX_STATIC_LINES> static_line_y_intervals;

	Max_Length_Array<Physics_Static_Circle, PHYSICS_MAX_STATIC_CIRCLES> static_circles;
	Max_Length_Array<Physics_Interval,      PHYSICS_MAX_STATIC_CIRCLES> static_circle_x_intervals;
	Max_Length_Array<Physics_Interval,      PHYSICS_MAX_STATIC_CIRCLES> static_circle_y_intervals;

	Max_Length_Array<Physics_Linear_Circle, PHYSICS_MAX_LINEAR_CIRCLES> linear_circles;
	Max_Length_Array<Physics_Interval,      PHYSICS_MAX_LINEAR_CIRCLES> linear_circle_x_intervals;
	Max_Length_Array<Physics_Interval,      PHYSICS_MAX_LINEAR_CIRCLES> linear_circle_y_intervals;

	Max_Length_Array<Physics_Collision, PHYSICS_MAX_COLLISIONS> collisions;
};

#define USE_PHYSICS_CONTEXT(name) \
	auto& static_lines = name->static_lines; \
	auto& static_line_x_intervals = name->static_line_x_intervals; \
	auto& static_line_y_intervals = name->static_line_y_intervals; \
	auto& static_circles = name->static_circles; \
	auto& static_circle_x_intervals = name->static_circle_x_intervals; \
	auto& static_circle_y_intervals = name->static_circle_y_intervals; \
	auto& linear_circles = name->linear_circles; \
	auto& linear_circle_x_intervals = name->linear_circle_x_intervals; \
	auto& linear_circle_y_intervals = name->linear_circle_y_intervals; \
	auto& collisions = name->collisions;

void physics_reset(Physics_Context* context)
{
	context->static_lines.reset();
	context->static_line_x_intervals.reset();
	context->static_line_y_intervals.reset();
	context->static_circles.reset();
	context->static_circle_x_intervals.reset();
	context->static_circle_y_intervals.reset();
	context->linear_circles.reset();
	context->linear_circle_x_intervals.reset();
	context->linear_circle_y_intervals.reset();
	context->collisions.reset();
}

static inline u8 physics_do_objects_collide(Physics_Object_Meta_Data *object_1,
                                            Physics_Object_Meta_Data *object_2)
{
	return (object_1->collides_with_mask & object_2->collision_mask
	     && object_2->collides_with_mask & object_1->collision_mask);
}

static void physics_sort_intervals(Slice<Physics_Interval> intervals)
{
	// XXX - use insertion sort for now because it's simple
	for (u32 i = 1; i < intervals.len; ++i) {
		Physics_Interval interval = intervals[i];
		u32 j = i;
		for ( ; j; --j) {
			if (intervals[j - 1].start <= interval.start) {
				break;
			}
			intervals[j] = intervals[j - 1];
		}
		intervals[j] = interval;
	}
}

void physics_start_frame(Physics_Context* context)
{
	USE_PHYSICS_CONTEXT(context)

	static_line_x_intervals.reset();
	static_line_y_intervals.reset();
	for (u32 i = 0; i < static_lines.len; ++i) {
		Physics_Static_Line line = static_lines[i];

		Physics_Interval interval_x;
		interval_x.object_index = i;
		interval_x.start = min(line.start.x, line.end.x);
		interval_x.end = max(line.start.x, line.end.x);
		static_line_x_intervals.append(interval_x);

		Physics_Interval interval_y;
		interval_y.object_index = i;
		interval_y.start = min(line.start.y, line.end.y);
		interval_y.end = max(line.start.y, line.end.y);
		static_line_y_intervals.append(interval_y);
	}

	static_circle_x_intervals.reset();
	static_circle_y_intervals.reset();
	for (u32 i = 0; i < static_circles.len; ++i) {
		Physics_Static_Circle circle = static_circles[i];

		Physics_Interval interval_x;
		interval_x.object_index = i;
		interval_x.start = circle.pos.x - circle.radius;
		interval_x.end = circle.pos.x + circle.radius;
		static_circle_x_intervals.append(interval_x);

		Physics_Interval interval_y;
		interval_y.object_index = i;
		interval_y.start = circle.pos.y - circle.radius;
		interval_y.end = circle.pos.y + circle.radius;
		static_circle_y_intervals.append(interval_y);
	}

	linear_circle_x_intervals.reset();
	linear_circle_y_intervals.reset();
	for (u32 i = 0; i < linear_circles.len; ++i) {
		Physics_Linear_Circle circle = linear_circles[i];

		v2 start = circle.start;
		v2 end = circle.start + circle.duration * circle.velocity;

		Physics_Interval interval_x;
		interval_x.object_index = i;
		interval_x.start = min(start.x, end.x) - circle.radius;
		interval_x.end = max(start.x, end.x) + circle.radius;
		linear_circle_x_intervals.append(interval_x);

		Physics_Interval interval_y;
		interval_y.object_index = i;
		interval_y.start = min(start.y, end.y) - circle.radius;
		interval_y.end = max(start.y, end.y) + circle.radius;
		linear_circle_y_intervals.append(interval_y);
	}

	physics_sort_intervals(static_line_x_intervals);
	physics_sort_intervals(static_line_y_intervals);
	physics_sort_intervals(static_circle_x_intervals);
	physics_sort_intervals(static_circle_y_intervals);
	physics_sort_intervals(linear_circle_x_intervals);
	physics_sort_intervals(linear_circle_y_intervals);
}

struct Physics_Interval_Overlap
{
	u32 object_index_1;
	u32 object_index_2;
};

static void physics_get_overlapping_intervals_1d(Slice<Physics_Interval>                 intervals_1,
                                                 Slice<Physics_Interval>                 intervals_2,
                                                 Output_Buffer<Physics_Interval_Overlap> output)
{
	output.reset();
	if (!intervals_1 || !intervals_2) {
		return;
	}

	Max_Length_Array<Physics_Interval, 1024> cur_intervals_1;
	Max_Length_Array<Physics_Interval, 1024> cur_intervals_2;
	cur_intervals_1.reset();
	cur_intervals_2.reset();

	f32 cur_x = min(intervals_1[0].start, intervals_1[0].start);

	u32 idx_1 = 0, idx_2 = 0;
	while (idx_1 < intervals_1.len || idx_2 < intervals_2.len) {
		f32 next_x = 1000.0f; // 1000.0f is definitely past the end of the map
		if (idx_1 < intervals_1.len) {
			Physics_Interval interval_1 = intervals_1[idx_1];
			if (interval_1.start == cur_x) {
				++idx_1;
				cur_intervals_1.append(interval_1);
				Physics_Interval_Overlap overlap;
				overlap.object_index_1 = interval_1.object_index;
				for (u32 i = 0; i < cur_intervals_2.len; ++i) {
					overlap.object_index_2 = cur_intervals_2[i].object_index;
					output.append(overlap);
				}
				continue;
			}
			next_x = interval_1.start;
		}
		if (idx_2 < intervals_2.len) {
			Physics_Interval interval_2 = intervals_2[idx_2];
			if (interval_2.start == cur_x) {
				++idx_2;
				cur_intervals_2.append(interval_2);
				Physics_Interval_Overlap overlap;
				overlap.object_index_2 = interval_2.object_index;
				for (u32 i = 0; i < cur_intervals_1.len; ++i) {
					overlap.object_index_1 = cur_intervals_1[i].object_index;
					output.append(overlap);
				}
				continue;
			}
			next_x = min(next_x, interval_2.start);
		}
		for (u32 i = 0; i < cur_intervals_1.len; ) {
			if (cur_intervals_1[i].end < next_x) {
				cur_intervals_1.remove(i);
				continue;
			}
			++i;
		}
		for (u32 i = 0; i < cur_intervals_2.len; ) {
			if (cur_intervals_2[i].end < next_x) {
				cur_intervals_2.remove(i);
				continue;
			}
			++i;
		}
		cur_x = next_x;
	}
}

static void physics_get_overlapping_intervals_2d(Slice<Physics_Interval> x_intervals_1,
                                                 Slice<Physics_Interval> y_intervals_1,
                                                 Slice<Physics_Interval> x_intervals_2,
                                                 Slice<Physics_Interval> y_intervals_2,
                                                 Output_Buffer<Physics_Interval_Overlap> output)
{
	Max_Length_Array<Physics_Interval_Overlap, 4096> overlapping_x_intervals;
	Max_Length_Array<Physics_Interval_Overlap, 4096> overlapping_y_intervals;
	physics_get_overlapping_intervals_1d(x_intervals_1, x_intervals_2, overlapping_x_intervals);
	physics_get_overlapping_intervals_1d(y_intervals_1, y_intervals_2, overlapping_y_intervals);

	output.reset();
	for (u32 i = 0; i < overlapping_x_intervals.len; ++i) {
		Physics_Interval_Overlap overlap_x = overlapping_x_intervals[i];
		for (u32 j = 0; j < overlapping_y_intervals.len; ++j) {
			Physics_Interval_Overlap overlap_y = overlapping_y_intervals[j];
			if (overlap_x.object_index_1 == overlap_y.object_index_1
			 && overlap_x.object_index_2 == overlap_y.object_index_2) {
				output.append(overlap_x);
			}
		}
	}
}

static u8 physics_get_linear_circle_origin_intersection_times(v2 p, v2 v, f32 r, f32* t_0, f32* t_1)
{
	// get times that a circle starting at point p travelling
	// at velocity v intersects with the origin
	// t_0 -- intersection begins
	// t_1 -- intersection ends
	// return -- whether or not intersection occurs

	f32 r_squared = r*r;
	f32 speed_squared = v.x*v.x + v.y*v.y;
	if (!speed_squared) {
		return 0;
	}

	f32 t_closest = -(v.x * p.x + v.y * p.y) / speed_squared;
	v2 u = p + t_closest * v;
	f32 a_squared = u.x*u.x + u.y*u.y;
	if (a_squared >= r_squared) {
		return 0;
	}

	f32 b_squared = r_squared - a_squared;
	f32 t_offset = sqrtf(b_squared / speed_squared);
	*t_0 = t_closest - t_offset;
	*t_1 = t_closest + t_offset;

	return 1;
}

void physics_debug_draw(Physics_Context* context, f32 time)
{
	USE_PHYSICS_CONTEXT(context)

	debug_draw_world_set_color(V4_f32(1.0f, 1.0f, 0.0f, 0.75f));

	for (u32 i = 0; i < static_lines.len; ++i) {
		Physics_Static_Line line = static_lines[i];
		debug_draw_world_line(line.start, line.end);
	}

	for (u32 i = 0; i < static_circles.len; ++i) {
		Physics_Static_Circle circle = static_circles[i];
		debug_draw_world_circle(circle.pos, circle.radius);
	}

	debug_draw_world_set_color(V4_f32(1.0f, 0.0f, 0.0f, 0.75f));

	for (u32 i = 0; i < linear_circles.len; ++i) {
		Physics_Linear_Circle circle = linear_circles[i];
		f32 dt = time - circle.start_time;
		if (dt < 0.0f || dt > circle.duration) {
			continue;
		}
		v2 p = circle.start + dt * circle.velocity;
		debug_draw_world_circle(p, circle.radius);
		debug_draw_world_line(circle.start, circle.start + circle.duration * circle.velocity);
	}
}

void physics_compute_collisions(Physics_Context* context, f32 start_time)
{
	USE_PHYSICS_CONTEXT(context)

	Max_Length_Array<Physics_Interval_Overlap, 4096> overlaps;

	collisions.reset();
	Physics_Collision collision = {};

	// static line linear circle collisions
	collision.objects_type = PHYSICS_COLLISION_STATIC_LINE_LINEAR_CIRCLE;
	physics_get_overlapping_intervals_2d(static_line_x_intervals,
	                                     static_line_y_intervals,
	                                     linear_circle_x_intervals,
	                                     linear_circle_y_intervals,
	                                     overlaps);
	for (u32 i = 0; i < overlaps.len; ++i) {
		Physics_Interval_Overlap overlap = overlaps[i];
		Physics_Static_Line *line = &static_lines[overlap.object_index_1];
		Physics_Linear_Circle *circle = &linear_circles[overlap.object_index_2];

		if (!physics_do_objects_collide(line, circle)) {
			continue;
		}

		f32 min_time = max(start_time, circle->start_time);
		f32 max_time = circle->start_time + circle->duration;
		if (min_time >= max_time) {
			continue;
		}

		v2 d = line->end - line->start;
		f32 theta = atan2f(d.y, d.x);

		f32 len = sqrtf(d.x*d.x + d.y*d.y);
		m2 m = m2::rotation(-theta);

		v2 p = m * (circle->start - line->start);
		v2 v = m * circle->velocity;
		f32 r = circle->radius;

		// handle parallel case separately
		if (!v.y) {
			if (fabsf(p.y) > r) {
				continue;
			}
			f32 r_squared = r*r;
			f32 offset = sqrtf(r_squared - p.y*p.y);
			f32 t_left = (-p.x - offset) / v.x;
			f32 t_right = (-p.x + len + offset) / v.x;
			f32 t_begin_penetrate = min(t_left, t_right);
			f32 t_end_penetrate = max(t_left, t_right);
			if (min_time < t_begin_penetrate && t_begin_penetrate < max_time) {
				collision.type =  PHYSICS_COLLISION_BEGIN_PENETRATION;
				collision.time = t_begin_penetrate;
				collision.static_line_linear_circle.static_line   = line;
				collision.static_line_linear_circle.linear_circle = circle;
				collisions.append(collision);
			}
			if (min_time < t_end_penetrate && t_end_penetrate < max_time) {
				collision.type = PHYSICS_COLLISION_END_PENETRATION;
				collision.time = t_end_penetrate;
				collision.static_line_linear_circle.static_line   = line;
				collision.static_line_linear_circle.linear_circle = circle;
				collisions.append(collision);
			}
			continue;
		}

		f32 t_0 = (r - p.y) / v.y;
		f32 t_1 = (- r - p.y) / v.y;
		f32 t_begin_penetrate = min(t_0, t_1);
		f32 t_end_penetrate = max(t_0, t_1);

		f32 p_begin_x = p.x + t_begin_penetrate * v.x;

		if (p_begin_x < 0.0f) {
			f32 t_begin, t_end;
			if (!physics_get_linear_circle_origin_intersection_times(p, v, r,
			                                                         &t_begin,
			                                                         &t_end)) {
				continue;
			}
			t_begin_penetrate = t_begin;
		} else if (p_begin_x > len) {
			v2 p_dash = p;
			p_dash.x -= len;
			f32 t_begin, t_end;
			if (!physics_get_linear_circle_origin_intersection_times(p_dash, v, r,
			                                                         &t_begin,
			                                                         &t_end)) {
				continue;
			}
			t_begin_penetrate = t_begin;
		}

		f32 p_end_x = p.x + t_end_penetrate * v.x;
		if (p_end_x < 0.0f) {
			f32 t_begin, t_end;
			if (!physics_get_linear_circle_origin_intersection_times(p, v, r,
			                                                         &t_begin,
			                                                         &t_end)) {
				// if we're here we _began_ penetrating, so should end it somewhere
				ASSERT(0);
			}
			t_end_penetrate = t_end;
		} else if (p_end_x > len) {
			v2 p_dash = p;
			p_dash.x -= len;
			f32 t_begin, t_end;
			if (!physics_get_linear_circle_origin_intersection_times(p_dash, v, r,
			                                                         &t_begin,
			                                                         &t_end)) {
				// same as above
				ASSERT(0);
			}
			t_end_penetrate = t_end;
		}

		t_begin_penetrate += circle->start_time;
		t_end_penetrate += circle->start_time;

		if (min_time < t_begin_penetrate && t_begin_penetrate < max_time) {
			collision.type =  PHYSICS_COLLISION_BEGIN_PENETRATION;
			collision.time = t_begin_penetrate;
			collision.static_line_linear_circle.static_line   = line;
			collision.static_line_linear_circle.linear_circle = circle;
			collisions.append(collision);
		}
		if (min_time < t_end_penetrate && t_end_penetrate < max_time) {
			collision.type = PHYSICS_COLLISION_END_PENETRATION;
			collision.time = t_end_penetrate;
			collision.static_line_linear_circle.static_line   = line;
			collision.static_line_linear_circle.linear_circle = circle;
			collisions.append(collision);
		}
	}

	// static circle linear circle collisions
	collision.objects_type = PHYSICS_COLLISION_STATIC_CIRCLE_LINEAR_CIRCLE;
	physics_get_overlapping_intervals_2d(static_circle_x_intervals,
	                                     static_circle_y_intervals,
	                                     linear_circle_x_intervals,
	                                     linear_circle_y_intervals,
	                                     overlaps);
	for (u32 i = 0; i < overlaps.len; ++i) {
		Physics_Interval_Overlap overlap = overlaps[i];
		Physics_Static_Circle *static_circle = &static_circles[overlap.object_index_1];
		Physics_Linear_Circle *linear_circle = &linear_circles[overlap.object_index_2];

		if (!physics_do_objects_collide(static_circle, linear_circle)) {
			continue;
		}

		f32 min_time = max(start_time, linear_circle->start_time);
		f32 max_time = linear_circle->start_time + linear_circle->duration;
		if (min_time >= max_time) {
			continue;
		}

		v2 p = linear_circle->start - static_circle->pos;
		v2 v = linear_circle->velocity;
		f32 r = linear_circle->radius + static_circle->radius;

		f32 t_begin_penetrate, t_end_penetrate;
		if (!physics_get_linear_circle_origin_intersection_times(p, v, r,
		                                                         &t_begin_penetrate,
		                                                         &t_end_penetrate)) {
			continue;
		}

		t_begin_penetrate += linear_circle->start_time;
		t_end_penetrate += linear_circle->start_time;

		if (min_time < t_begin_penetrate && t_begin_penetrate < max_time) {
			collision.type =  PHYSICS_COLLISION_BEGIN_PENETRATION;
			collision.time = t_begin_penetrate;
			collision.static_circle_linear_circle.static_circle = static_circle;
			collision.static_circle_linear_circle.linear_circle = linear_circle;
			collisions.append(collision);
		}
		if (min_time < t_end_penetrate && t_end_penetrate < max_time) {
			collision.type = PHYSICS_COLLISION_END_PENETRATION;
			collision.time = t_end_penetrate;
			collision.static_circle_linear_circle.static_circle = static_circle;
			collision.static_circle_linear_circle.linear_circle = linear_circle;
			collisions.append(collision);
		}
	}

	// linear circle linear circle collisions
	collision.objects_type = PHYSICS_COLLISION_LINEAR_CIRCLE_LINEAR_CIRCLE;
	physics_get_overlapping_intervals_2d(linear_circle_x_intervals,
	                                     linear_circle_y_intervals,
	                                     linear_circle_x_intervals,
	                                     linear_circle_y_intervals,
	                                     overlaps);
	for (u32 i = 0; i < overlaps.len; ++i) {
		Physics_Interval_Overlap overlap = overlaps[i];
		// filter out duplicate/self-self overlaps
		if (overlap.object_index_1 >= overlap.object_index_2) {
			continue;
		}
		Physics_Linear_Circle *circle_1 = &linear_circles[overlap.object_index_1];
		Physics_Linear_Circle *circle_2 = &linear_circles[overlap.object_index_2];

		if (!physics_do_objects_collide(circle_1, circle_2)) {
			continue;
		}

		f32 min_time = max(max(start_time, circle_1->start_time), circle_2->start_time);
		f32 max_time = min(circle_1->start_time + circle_1->duration,
		                   circle_2->start_time + circle_2->duration);

		if (min_time >= max_time) {
			continue;
		}

		f32 t_start_offset = circle_2->start_time - circle_1->start_time;
		v2 p = circle_1->start - (circle_2->start + t_start_offset * circle_2->velocity);
		v2 v = circle_1->velocity - circle_2->velocity;
		f32 r = circle_1->radius + circle_2->radius;

		f32 t_begin_penetrate, t_end_penetrate;
		if (!physics_get_linear_circle_origin_intersection_times(p, v, r,
		                                                         &t_begin_penetrate,
		                                                         &t_end_penetrate)) {
			continue;
		}

		t_begin_penetrate += circle_1->start_time;
		t_end_penetrate += circle_1->start_time;

		if (min_time < t_begin_penetrate && t_begin_penetrate < max_time) {
			collision.type =  PHYSICS_COLLISION_BEGIN_PENETRATION;
			collision.time = t_begin_penetrate;
			collision.linear_circle_linear_circle.linear_circle_1 = circle_1;
			collision.linear_circle_linear_circle.linear_circle_2 = circle_2;
			collisions.append(collision);
		}
		if (min_time < t_end_penetrate && t_end_penetrate < max_time) {
			collision.type = PHYSICS_COLLISION_END_PENETRATION;
			collision.time = t_end_penetrate;
			collision.linear_circle_linear_circle.linear_circle_1 = circle_1;
			collision.linear_circle_linear_circle.linear_circle_2 = circle_2;
			collisions.append(collision);
		}
	}

	// sort collisions
	for (u32 i = 1; i < collisions.len; ++i) {
		Physics_Collision collision = collisions[i];
		u32 j = i;
		for ( ; j; --j) {
			if (collisions[j - 1].time <= collision.time) {
				break;
			}
			collisions[j] = collisions[j - 1];
		}
		collisions[j] = collision;
	}
}

void physics_remove_objects_for_entity(Physics_Context* context, Entity_ID entity_id)
{
	USE_PHYSICS_CONTEXT(context)

	for (u32 i = 0; i < static_circles.len; ) {
		if (static_circles[i].owner_id == entity_id) {
			// TODO -- update collision objects here?
			static_circles.remove(i);
			continue;
		}
		++i;
	}

	for (u32 i = 0; i < linear_circles.len; ) {
		if (linear_circles[i].owner_id == entity_id) {
			linear_circles.remove(i);
			continue;
		}
		++i;
	}
}

#endif
