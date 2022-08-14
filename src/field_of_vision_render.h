#pragma once

#include "prelude.h"
#include "containers.hpp"
#include "types.h"

#include "fov.h"

#include "field_of_vision_render_gpu_data_types.h"

// XXX - need these definitions here

#define FOV_RENDER_MAX_BUFFERS 8
#define FOV_RENDER_MAX_EDGE_INSTANCES (256 * 256)
#define FOV_RENDER_MAX_FILL_INSTANCES (256 * 256)

struct Field_Of_Vision_Draw
{
	Field_Of_Vision *fov;
	u32              id;
};

struct Field_Of_Vision_Entry
{
	u32 id;
	f32 alpha;
};

struct Field_Of_Vision_Render
{
	v2_u32 grid_size;
	Max_Length_Array<Field_Of_Vision_Draw, FOV_RENDER_MAX_BUFFERS> fov_draws;
	Stack<u32, FOV_RENDER_MAX_BUFFERS> free_buffers;
	Max_Length_Array<Field_Of_Vision_Entry, FOV_RENDER_MAX_BUFFERS> fov_entries;
};

void fov_render_init(Field_Of_Vision_Render* render, v2_u32 grid_size);
void fov_render_reset(Field_Of_Vision_Render* render);
u32 fov_render_add_fov(Field_Of_Vision_Render* render, Field_Of_Vision* fov);
void fov_render_release_buffer(Field_Of_Vision_Render* render, u32 buffer_id);
void fov_render_set_alpha(Field_Of_Vision_Render* render, u32 buffer_id, f32 alpha);