#include "jfg_math.h"

static void calc_line_aux(v2_i32 start, v2_i32 end, Output_Buffer<v2_i32> line)
{
	line.reset();
	i32 y = start.y;
	i32 height = abs(end.y - start.y);
	i32 length = abs(end.x - start.x);
	i32 dy = height ? (end.y - start.y) / height : 0;
	i32 acc = 2*height - length;
	for (i32 x = start.x; x <= end.x; ++x) {
		line.append(v2_i32(x, y));
		acc += 2*height;
		if (acc > 0) {
			acc -= 2*length;
			y += dy;
		}
	}
}

void calc_line(v2_i32 start, v2_i32 end, Output_Buffer<v2_i32> line)
{
	i32 x_pos = end.x - start.x;
	i32 x_neg = start.x - end.x;
	i32 y_pos = end.y - start.y;
	i32 y_neg = start.y - end.y;
	i32 max = jfg_max(x_pos, x_neg, y_pos, y_neg);
	if (x_pos == max) {
		calc_line_aux(start, end, line);
	} else if (x_neg == max) {
		calc_line_aux(v2_i32(-start.x, start.y), v2_i32(-end.x, end.y), line);
		for (u32 i = 0; i < *line.len; ++i) {
			line[i].x = -line[i].x;
		}
	} else if (y_pos == max) {
		calc_line_aux(v2_i32(start.y, start.x), v2_i32(end.y, end.x), line);
		for (u32 i = 0; i < *line.len; ++i) {
			auto tmp = line[i].x;
			line[i].x = line[i].y;
			line[i].y = tmp;
		}
	} else {
		calc_line_aux(v2_i32(-start.y, start.x), v2_i32(-end.y, end.x), line);
		for (u32 i = 0; i < *line.len; ++i) {
			auto tmp = line[i].x;
			line[i].x = line[i].y;
			line[i].y = -tmp;
		}
	}
}