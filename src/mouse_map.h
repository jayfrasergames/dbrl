#ifndef MOUSE_MAP_H
#define MOUSE_MAP_H

#include "jfg/prelude.h"

struct Mouse_Map_Header
{
	v2_u32 dimensions;
	v2_u32 tile_size;
	u8 *data;
};

u8 mouse_map_check(Mouse_Map_Header* mouse_map, v2_u32 position);

#ifndef JFG_HEADER_ONLY
u8 mouse_map_check(Mouse_Map_Header* mouse_map, v2_u32 position)
{
	u32 index = position.y * mouse_map->dimensions.w + position.x;
	u32 array_index = index >> 3;
	u32 bit_mask = 1 << (index & 7);
	return mouse_map->data[array_index] & bit_mask;
}
#endif

#endif
