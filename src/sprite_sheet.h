#pragma once

#include "prelude.h"
#include "containers.hpp"

#include "particles.h"

// =============================================================================
// Sprite_Sheet data defintion

#include "sprite_sheet_gpu_data_types.h"

struct Sprite_Sheet_Data
{
	v2_u32 size;
	v2_u32 sprite_size;
	u32 *image_data;
	u8  *mouse_map_data;
};

#define SPRITE_SHEET_MAX_INSTANCES 10240
struct Sprite_Sheet_Instances
{
	Sprite_Sheet_Data data;

	// u32 num_instances;
	// Sprite_Sheet_Instance instances[SPRITE_SHEET_MAX_INSTANCES];
	Max_Length_Array<Sprite_Sheet_Instance, SPRITE_SHEET_MAX_INSTANCES> instances;
};

struct Sprite_Sheet_Font_Instances
{
	v2_u32  tex_size;
	void   *tex_data;

	Max_Length_Array<Sprite_Sheet_Font_Instance, SPRITE_SHEET_MAX_INSTANCES> instances;
};

struct Sprite_Sheet_Renderer
{
	v2_u32 size;
	u32                                highlighted_sprite;
	u32                                num_instance_buffers;
	Sprite_Sheet_Instances*            instance_buffers;
	Slice<Sprite_Sheet_Font_Instances> font_instance_buffers;
	Particles                          particles;
	f32                                time;
};

void sprite_sheet_renderer_init(Sprite_Sheet_Renderer* renderer,
                                Sprite_Sheet_Instances* instance_buffers,
                                u32 num_instance_buffers,
                                v2_u32 size);
u32 sprite_sheet_renderer_id_in_pos(Sprite_Sheet_Renderer* renderer, v2_u32 pos);
void sprite_sheet_renderer_highlight_sprite(Sprite_Sheet_Renderer* renderer, u32 sprite_id);

void sprite_sheet_instances_reset(Sprite_Sheet_Instances* instances);
void sprite_sheet_instances_add(Sprite_Sheet_Instances* instances, Sprite_Sheet_Instance instance);

void sprite_sheet_font_instances_reset(Sprite_Sheet_Font_Instances* instances);
void sprite_sheet_font_instances_add(Sprite_Sheet_Font_Instances* instances,
                                     Sprite_Sheet_Font_Instance   instance);