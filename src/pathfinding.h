#pragma once

#include "prelude.h"
#include "types.h"

typedef Map_Cache<u32> Dijkstra_Map;

void calc_dijkstra_map(Map_Cache_Bool* can_pass, Pos goal, Dijkstra_Map* map);
void debug_draw_dijkstra_map(Dijkstra_Map* map);