#include "render.h"

#include "stdafx.h"

#define RENDER_JOB_BUFFER_SIZE 1024*1024

bool init(Render* render)
{
	memset(render, 0, sizeof(*render));
	void *buffer = malloc(RENDER_JOB_BUFFER_SIZE);
	if (!buffer) {
		return false;
	}

	init(&render->render_job_buffer, buffer, RENDER_JOB_BUFFER_SIZE);

	return true;
}

static void push(Render_Job_Buffer* buffer, void* dst, void* src, size_t size)
{
	ASSERT((uptr)dst - (uptr)buffer->base + size <= buffer->size);
	memcpy(dst, src, size);
}

void init(Render_Job_Buffer* buffer, void* base, size_t size)
{
	buffer->base = base;
	buffer->cur_pos = base;
	buffer->size = size;

	auto job = (Render_Job*)buffer->cur_pos;
	job->type = RENDER_JOB_NONE;
}

void reset(Render_Job_Buffer* buffer)
{
	buffer->cur_pos = buffer->base;
	auto job = (Render_Job*)buffer->cur_pos;
	job->type = RENDER_JOB_NONE;
}

template<typename T>
static size_t end_size(End_Of_Struct_Array<T> xs)
{
	return sizeof(xs.items[0]) + xs.len;
}

size_t size(Render_Job* job)
{
	size_t result = (size_t)OFFSET_OF(Render_Job, triangles);
	switch (job->type) {
	case RENDER_JOB_NONE:
		break;
	case RENDER_JOB_TRIANGLES:
		result += sizeof(job->triangles) + end_size(job->triangles.triangles);
		break;
	case RENDER_JOB_TYPE_SPRITES:
		result += sizeof(job->sprites) + end_size(job->sprites.instances);
		break;
	}
	return align(result, alignof(Render_Job));
}

Render_Job* next_job(Render_Job_Buffer* buffer)
{
	auto job = (Render_Job*)buffer->cur_pos;
	buffer->cur_pos = (void*)((uptr)job + size(job));
	ASSERT((uptr)buffer->cur_pos - (uptr)buffer->base <= buffer->size);
	return (Render_Job*)buffer->cur_pos;
}

void end(Render_Job_Buffer* buffer)
{
	next_job(buffer);
}

void begin_triangles(Render_Job_Buffer* buffer, Debug_Draw_World_Constant_Buffer constants)
{
	auto job = (Render_Job*)buffer->cur_pos;
	job->type = RENDER_JOB_TRIANGLES;
	job->triangles.constants = constants;
	job->triangles.triangles.len = 0;
}

void push_triangle(Render_Job_Buffer* buffer, Debug_Draw_World_Triangle triangle)
{
	auto job = (Render_Job*)buffer->cur_pos;
	ASSERT(job->type == RENDER_JOB_TRIANGLES);
	void *buffer_end = &job->triangles.triangles.items[job->triangles.triangles.len];
	push(buffer, buffer_end, &triangle, sizeof(triangle));
	job->triangles.triangles.len += 1;
}

void begin_sprites(Render_Job_Buffer* buffer, Texture_ID texture_id)
{
	auto job = (Render_Job*)buffer->cur_pos;
	job->type = RENDER_JOB_TYPE_SPRITES;
	job->sprites.sprite_sheet_id = texture_id;
}

void push_sprite(Render_Job_Buffer* buffer, Render_Sprites_Instance instance)
{
	auto job = (Render_Job*)buffer->cur_pos;
	push(&job->sprites.instances, instance, (void*)((uptr)buffer->base + buffer->size));
}

Texture_ID load_texture(Render* render, const char* filename, Platform_Functions* platform_functions)
{
	for (u32 i = 0; i < render->textures.len; ++i) {
		if (strcmp(filename, render->textures[i].filename) == 0) {
			return i;
		}
	}

	ASSERT(has_space(render->textures));
	auto texture = load_texture(filename, platform_functions);
	ASSERT(texture);	
	if (!texture) {
		return 0xFFFFFFFF;
	}

	Texture_ID tex_id = render->textures.len++;
	auto *new_tex = &render->textures.items[tex_id];
	strncpy(new_tex->filename, filename, ARRAY_SIZE(new_tex->filename));
	new_tex->texture = texture;

	return tex_id;
}