#include "pathfinding.h"

#include "containers.hpp"
#include "debug_draw_world.h"

void calc_dijkstra_map(Map_Cache_Bool* _can_pass, Pos goal, Dijkstra_Map* _map)
{
	auto &can_pass = *_can_pass;
	auto &map = *_map;

	for (u32 y = 0; y < 256; ++y) {
		for (u32 x = 0; x < 256; ++x) {
			Pos p = Pos(x, y);
			map[p] = (u32)-1;
		}
	}

	struct To_Expand
	{
		Pos pos;
		u32 cost;
	};

	Map_Cache_Bool visited = {};
	Max_Length_Array<To_Expand, 256*256> to_expand;
	to_expand.reset();

	to_expand.append({ goal, 0 });
	visited.set(goal);

	for (u32 i = 0; i < to_expand.len; ++i) {
		auto vertex = to_expand[i];
		map[vertex.pos] = vertex.cost;

		for (u32 y = vertex.pos.y - 1; y <= vertex.pos.y + 1; ++y) {
			for (u32 x = vertex.pos.x - 1; x <= vertex.pos.x + 1; ++x) {
				Pos p = Pos(x, y);
				if (vertex.pos == p || !can_pass.get(p) || visited.get(p)) {
					continue;
				}
				visited.set(p);
				To_Expand new_vertex = {};
				new_vertex.pos = p;
				new_vertex.cost = vertex.cost + 1;
				to_expand.append(new_vertex);
			}
		}
	}
}

void debug_draw_dijkstra_map(Dijkstra_Map* _map)
{
	auto &map = *_map;

	debug_draw_world_set_color(v4(1.0f, 0.0f, 0.0f, 0.5f));
	for (u32 y = 1; y < 255; ++y) {
		for (u32 x = 1; x < 255; ++x) {
			Pos p = Pos(x, y);
			for (u32 y2 = y - 1; y2 <= y + 1; ++y2) {
				for (u32 x2 = x - 1; x2 <= x + 1; ++x2) {
					Pos q = Pos(x2, y2);
					if (map[p] && map[q] == map[p] - 1) {
						debug_draw_world_arrow((v2)p, (v2)q, 0.25f);
						// debug_draw_world_line((v2)p, (v2)q);
					}
				}
			}
		}
	}
}