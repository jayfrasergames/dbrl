#define JFG_HEADER_ONLY
#include "jfg_d3d11.h"

#include "sprite_sheet.h"

#include "stdafx.h"

void sprite_sheet_renderer_init(Sprite_Sheet_Renderer* renderer,
                                Sprite_Sheet_Instances* instance_buffers,
                                u32 num_instance_buffers,
                                v2_u32 size)
{
	particles_init(&renderer->particles);
	renderer->size = size;
	renderer->num_instance_buffers = num_instance_buffers;
	renderer->instance_buffers = instance_buffers;
}

u32 sprite_sheet_renderer_id_in_pos(Sprite_Sheet_Renderer* renderer, v2_u32 pos)
{
	u32 best_id = 0;
	f32 best_depth = 0.0f;
	for (u32 i = 0; i < renderer->num_instance_buffers; ++i) {
		Sprite_Sheet_Instances* instances = &renderer->instance_buffers[i];
		u32 num_instances  = instances->instances.len;
		u32 tex_width      = instances->data.size.w;
		v2_u32 sprite_size = instances->data.sprite_size;
		for (u32 j = 0; j < num_instances; ++j) {
			Sprite_Sheet_Instance *instance = &instances->instances[j];
			v2_u32 top_left = (v2_u32)(instance->world_pos * (v2)sprite_size);
			top_left.y += (u32)instance->y_offset;
			if (top_left.x > pos.x || top_left.y > pos.y) {
				continue;
			}
			v2_u32 tile_coord = { pos.x - top_left.x, pos.y - top_left.y };
			if (tile_coord.x >= sprite_size.w || tile_coord.y >= sprite_size.h) {
				continue;
			}
			v2_u32 tex_coord = {
				((u32)instance->sprite_pos.x) * sprite_size.w + tile_coord.x,
				((u32)instance->sprite_pos.y) * sprite_size.h + tile_coord.y
			};
			u32 index = tex_coord.y * tex_width + tex_coord.x;
			u32 array_index = index >> 3;
			u32 bit_mask = 1 << (index & 7);
			if (!(instances->data.mouse_map_data[array_index] & bit_mask)) {
				continue;
			}
			f32 depth = instance->depth_offset + instance->world_pos.y;
			if (depth > best_depth) {
				best_id = instance->sprite_id;
				best_depth = depth;
			}
		}
	}
	return best_id;
}

void sprite_sheet_renderer_highlight_sprite(Sprite_Sheet_Renderer* renderer, u32 sprite_id)
{
	renderer->highlighted_sprite = sprite_id ? sprite_id : (u32)-1;
}

void sprite_sheet_instances_reset(Sprite_Sheet_Instances* instances)
{
	instances->instances.reset();
}

void sprite_sheet_instances_add(Sprite_Sheet_Instances* instances, Sprite_Sheet_Instance instance)
{
	instances->instances.append(instance);
}

void sprite_sheet_font_instances_reset(Sprite_Sheet_Font_Instances* instances)
{
	instances->instances.reset();
}

void sprite_sheet_font_instances_add(Sprite_Sheet_Font_Instances* instances,
                                     Sprite_Sheet_Font_Instance   instance)
{
	instances->instances.append(instance);
}