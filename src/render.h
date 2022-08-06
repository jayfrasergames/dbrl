#pragma once

#include "prelude.h"
#include "texture.h"
#include "containers.hpp"

#include "debug_draw_world_gpu_data_types.h"
#include "triangle_gpu_data_types.h"
#include "sprites_gpu_data_types.h"

#define MAX_TEXTURES             32
#define MAX_TEXTURE_FILENAME_LEN 256

typedef u32 Texture_ID;

enum Render_Job_Type
{
	RENDER_JOB_NONE,
	RENDER_JOB_TRIANGLES,
	RENDER_JOB_TYPE_SPRITES,
};

struct Render_Job
{
	Render_Job_Type type;
	union {
		struct {
			Debug_Draw_World_Constant_Buffer               constants;
			End_Of_Struct_Array<Debug_Draw_World_Triangle> triangles;
		} triangles;
		struct {
			Texture_ID                           sprite_sheet_id;
			Sprite_Constants                     constants;
			End_Of_Struct_Array<Sprite_Instance> instances;
		} sprites;
	};
};

struct Render_Texture
{
	Texture *texture;
	char     filename[MAX_TEXTURE_FILENAME_LEN];
};

struct Render_Job_Buffer
{
	void   *base;
	void   *cur_pos;
	size_t  size;
};

struct Render
{
	Max_Length_Array<Render_Texture, MAX_TEXTURES> textures;
	Render_Job_Buffer render_job_buffer;
};

bool init(Render* render);
Texture_ID load_texture(Render* render, const char* filename, Platform_Functions* platform_functions);

void init(Render_Job_Buffer* buffer, void* base, size_t size);

void reset(Render_Job_Buffer* buffer);
void end(Render_Job_Buffer* buffer);
Render_Job* next_job(Render_Job_Buffer* buffer);

void begin_triangles(Render_Job_Buffer* buffer, Debug_Draw_World_Constant_Buffer constants);
void push_triangle(Render_Job_Buffer* buffer, Debug_Draw_World_Triangle triangle);

void begin_sprites(Render_Job_Buffer* buffer, Texture_ID texture_id, Sprite_Constants constants);
void push_sprite(Render_Job_Buffer* buffer, Sprite_Instance instance);