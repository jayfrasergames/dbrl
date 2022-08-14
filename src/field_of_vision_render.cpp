#include "field_of_vision_render.h"

#include "stdafx.h"

void fov_render_init(Field_Of_Vision_Render* render, v2_u32 grid_size)
{
	render->grid_size = grid_size;
	fov_render_reset(render);
}

void fov_render_reset(Field_Of_Vision_Render* render)
{
	render->fov_draws.reset();
	render->free_buffers.reset();
	for (u32 i = 0; i < FOV_RENDER_MAX_BUFFERS; ++i) {
		render->free_buffers.push(i + 1);
	}
}

u32 fov_render_add_fov(Field_Of_Vision_Render* render, Field_Of_Vision* fov)
{
	u32 buffer_id = render->free_buffers.pop();
	Field_Of_Vision_Draw draw = {};
	draw.fov = fov;
	draw.id = buffer_id;
	render->fov_draws.append(draw);
	Field_Of_Vision_Entry entry = {};
	entry.id = buffer_id;
	entry.alpha = 0.0f;
	render->fov_entries.append(entry);
	return buffer_id;
}

void fov_render_release_buffer(Field_Of_Vision_Render* render, u32 buffer_id)
{
	render->free_buffers.push(buffer_id);
	for (u32 i = 0; i < render->fov_draws.len; ++i) {
		if (render->fov_draws[i].id == buffer_id) {
			render->fov_draws.remove(i);
			break;
		}
	}
	for (u32 i = 0; i < render->fov_entries.len; ++i) {
		if (render->fov_entries[i].id == buffer_id) {
			render->fov_entries.remove_preserve_order(i);
			return;
		}
	}
	ASSERT(0);
}

void fov_render_set_alpha(Field_Of_Vision_Render* render, u32 buffer_id, f32 alpha)
{
	for (u32 i = 0; i < render->fov_entries.len; ++i) {
		if (render->fov_entries[i].id == buffer_id) {
			render->fov_entries[i].alpha = alpha;
			break;
		}
	}
}