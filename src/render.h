#pragma once

#include "prelude.h"
#include "texture.h"
#include "containers.hpp"
#include "jfg_error.h"

#include "sprites_gpu_data_types.h"
#include "triangle_gpu_data_types.h"

#define MAX_TEXTURES             32
#define MAX_TEXTURE_FILENAME_LEN 256

typedef u32 Texture_ID;
#define INVALID_TEXTURE_ID 0xFFFFFFFF

enum Render_Job_Type
{
	RENDER_JOB_NONE,
	RENDER_JOB_TRIANGLES,
	RENDER_JOB_SPRITES,
};

struct Render_Job
{
	Render_Job_Type type;
	union {
		struct {
			u32 start;
			u32 count;
		} triangles;
		struct {
			Texture_ID       sprite_sheet_id;
			Sprite_Constants constants;
			u32              start;
			u32              count;
		} sprites;
	};
};

struct Render_Texture
{
	Texture *texture;
	char     filename[MAX_TEXTURE_FILENAME_LEN];
};

#define FORMATS \
	FORMAT(R32G32_F32) \
	FORMAT(R32G32B32A32_F32)

enum Format
{
#define FORMAT(name) FORMAT_##name,
	FORMATS
#undef FORMAT
};

// INSTANCE_BUFFER(name, type, max_elems)
#define INSTANCE_BUFFERS \
	INSTANCE_BUFFER(SPRITE,   Sprite_Instance,   2048) \
	INSTANCE_BUFFER(TRIANGLE, Triangle_Instance, 2048)

// TODO -- offset?
// INPUT_ELEM(instance_buffer_name, semantic_name, format)
#define INSTANCE_BUFFER_INPUT_ELEMS \
	INPUT_ELEM(SPRITE, "GLYPH_COORDS",  FORMAT_R32G32_F32) \
	INPUT_ELEM(SPRITE, "OUTPUT_COORDS", FORMAT_R32G32_F32) \
	INPUT_ELEM(SPRITE, "COLOR",         FORMAT_R32G32B32A32_F32) \
	\
	INPUT_ELEM(TRIANGLE, "VERTEX_A", FORMAT_R32G32_F32) \
	INPUT_ELEM(TRIANGLE, "VERTEX_B", FORMAT_R32G32_F32) \
	INPUT_ELEM(TRIANGLE, "VERTEX_C", FORMAT_R32G32_F32) \
	INPUT_ELEM(TRIANGLE, "COLOR",    FORMAT_R32G32B32A32_F32)

// XXX -- dirty hack but oh well
#define INPUT_ELEM(...) +1
const u32 NUM_INPUT_ELEMS = 0 INSTANCE_BUFFER_INPUT_ELEMS;
#undef INPUT_ELEM

enum Instance_Buffer_ID
{
#define INSTANCE_BUFFER(name, _type, _max_elems) INSTANCE_BUFFER_##name,
	INSTANCE_BUFFERS
#undef INSTANCE_BUFFER

	NUM_INSTANCE_BUFFERS
};

struct Instance_Buffer_Metadata
{
	size_t size;
	size_t element_size;
	u32    max_elems;
};

extern Instance_Buffer_Metadata INSTANCE_BUFFER_METADATA[NUM_INSTANCE_BUFFERS];

struct Instance_Buffer_Input_Elem_Metadata
{
	Instance_Buffer_ID  instance_buffer_id;
	const char         *semantic_name;
	Format              format;
};

extern Instance_Buffer_Input_Elem_Metadata INSTANCE_BUFFER_INPUT_ELEM_METADATA[NUM_INPUT_ELEMS];

struct Instance_Buffer
{
	void   *base;
	void   *buffer_head;
	u32     cur_pos;
};

struct Render_Job_Buffer
{
	Max_Length_Array<Render_Job, 64> jobs;
	Instance_Buffer                  instance_buffers[NUM_INSTANCE_BUFFERS];
};

struct Render
{
	v2_u32 screen_size;
	Max_Length_Array<Render_Texture, MAX_TEXTURES> textures;
	Render_Job_Buffer render_job_buffer;
};

JFG_Error init(Render* render);
Texture_ID get_texture_id(Render* render, const char* filename);
JFG_Error load_textures(Render* render, Platform_Functions* platform_functions);

void reset(Render_Job_Buffer* buffer);
Render_Job* next_job(Render_Job_Buffer* buffer);

void push_triangle(Render_Job_Buffer* buffer, Triangle_Instance instance);

void begin_sprites(Render_Job_Buffer* buffer, Texture_ID texture_id, Sprite_Constants constants);
void push_sprite(Render_Job_Buffer* buffer, Sprite_Instance instance);