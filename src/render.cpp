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
	// job->type = RENDER_JOB_NONE;
	memset(job, 0, sizeof(*job));

	// XXX
	memset(buffer->base, 0, buffer->size);
}

template<typename T>
static size_t end_size(End_Of_Struct_Array<T> xs)
{
	return sizeof(xs.items[0]) * xs.len;
}

size_t job_size(Render_Job* job)
{
	switch (job->type) {
	case RENDER_JOB_NONE:
		return OFFSET_OF(Render_Job, triangles);
	case RENDER_JOB_TRIANGLES:
		return OFFSET_OF(Render_Job, triangles.triangles.items) + end_size(job->triangles.triangles);
	case RENDER_JOB_TYPE_SPRITES:
		return OFFSET_OF(Render_Job, sprites.instances.items) + end_size(job->sprites.instances);
	}
	ASSERT(false);
	return (size_t)-1;
}

static void push(Render_Job_Buffer* buffer, void* src, size_t size)
{
	auto job = (Render_Job*)buffer->cur_pos;
	size_t tmp = job_size(job);
	void *dst = (void*)((uptr)job + job_size(job));
	ASSERT((uptr)dst - (uptr)buffer->base + size <= buffer->size);
	memcpy(dst, src, size);
}

Render_Job* next_job(Render_Job_Buffer* buffer)
{
	auto job = (Render_Job*)buffer->cur_pos;
	buffer->cur_pos = (void*)((uptr)job + job_size(job));
	buffer->cur_pos = (void*)align((uptr)buffer->cur_pos, alignof(Render_Job));
	ASSERT((uptr)buffer->cur_pos - (uptr)buffer->base + sizeof(Render_Job) <= buffer->size);
	return (Render_Job*)buffer->cur_pos;
}

void end(Render_Job_Buffer* buffer)
{
	next_job(buffer);
	memset(buffer->cur_pos, 0, sizeof(Render_Job));
}

void begin_triangles(Render_Job_Buffer* buffer, Debug_Draw_World_Constant_Buffer constants)
{
	auto job = (Render_Job*)buffer->cur_pos;
	ASSERT(job->type == RENDER_JOB_NONE);
	job->type = RENDER_JOB_TRIANGLES;
	job->triangles.constants = constants;
	job->triangles.triangles.len = 0;
}

void push_triangle(Render_Job_Buffer* buffer, Debug_Draw_World_Triangle triangle)
{
	auto job = (Render_Job*)buffer->cur_pos;
	ASSERT(job->type == RENDER_JOB_TRIANGLES);
	void *buffer_end = &job->triangles.triangles.items[job->triangles.triangles.len];
	push(buffer, &triangle, sizeof(triangle));
	job->triangles.triangles.len += 1;
}

void begin_sprites(Render_Job_Buffer* buffer, Texture_ID texture_id, Sprite_Constants constants)
{
	auto job = (Render_Job*)buffer->cur_pos;
	ASSERT(job->type == RENDER_JOB_NONE);
	job->type = RENDER_JOB_TYPE_SPRITES;
	job->sprites.sprite_sheet_id = texture_id;
	job->sprites.constants = constants;
}

void push_sprite(Render_Job_Buffer* buffer, Sprite_Instance instance)
{
	auto job = (Render_Job*)buffer->cur_pos;
	ASSERT(job->type == RENDER_JOB_TYPE_SPRITES);
	push(buffer, &instance, sizeof(instance));
	job->sprites.instances.len += 1;
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