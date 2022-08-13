#include "render.h"

#include "stdafx.h"

#define RENDER_JOB_BUFFER_SIZE 1024*1024

Instance_Buffer_Metadata INSTANCE_BUFFER_METADATA[] = {
#define INSTANCE_BUFFER(_name, type, max_elems) { sizeof(type) * max_elems, sizeof(type), max_elems },
	INSTANCE_BUFFERS
#undef INSTANCE_BUFFER
};

Instance_Buffer_Input_Elem_Metadata INSTANCE_BUFFER_INPUT_ELEM_METADATA[] = {
#define INPUT_ELEM(instance_buffer_id, semantic_name, format) { INSTANCE_BUFFER_##instance_buffer_id, semantic_name, format },
	INSTANCE_BUFFER_INPUT_ELEMS
#undef INPUT_ELEM
};

JFG_Error init(Render* render)
{
	memset(render, 0, sizeof(*render));

	return JFG_SUCCESS;
}

void reset(Render_Job_Buffer* buffer)
{
	buffer->jobs.reset();
	for (u32 i = 0; i < ARRAY_SIZE(buffer->instance_buffers); ++i) {
		auto ib = &buffer->instance_buffers[i];
		ib->buffer_head = ib->base;
		ib->cur_pos = 0;
	}
}

template <typename T>
static inline void instance_buffer_push(Render_Job_Buffer* buffer, Instance_Buffer_ID ib_id, T instance)
{
	auto ib = &buffer->instance_buffers[ib_id];
	ASSERT(ib->cur_pos < INSTANCE_BUFFER_METADATA[ib_id].max_elems);
	memcpy(ib->buffer_head, &instance, sizeof(instance));
	ib->buffer_head = (void*)((uptr)ib->buffer_head + sizeof(instance));
	++ib->cur_pos;
}

static inline Render_Job* cur_job(Render_Job_Buffer* buffer)
{
	if (buffer->jobs) {
		return &buffer->jobs[buffer->jobs.len - 1];
	}
	return NULL;
}

void push_triangle(Render_Job_Buffer* buffer, Triangle_Instance instance)
{
	auto job = cur_job(buffer);
	if (job == NULL || job->type != RENDER_JOB_TRIANGLES) {
		job = buffer->jobs.append();
		job->type = RENDER_JOB_TRIANGLES;
		job->triangles.start = buffer->instance_buffers[INSTANCE_BUFFER_TRIANGLE].cur_pos;
		job->triangles.count = 0;
	}
	instance_buffer_push(buffer, INSTANCE_BUFFER_TRIANGLE, instance);
	++job->triangles.count;
}

void begin_sprites(Render_Job_Buffer* buffer, Texture_ID texture_id, Sprite_Constants constants)
{
	Render_Job job = {};
	job.type = RENDER_JOB_SPRITES;
	job.sprites.sprite_sheet_id = texture_id;
	job.sprites.constants = constants;
	job.sprites.start = buffer->instance_buffers[INSTANCE_BUFFER_SPRITE].cur_pos;
	job.sprites.count = 0;
	buffer->jobs.append(job);
}

void push_sprite(Render_Job_Buffer* buffer, Sprite_Instance instance)
{
	auto job = cur_job(buffer);
	ASSERT(job && job->type == RENDER_JOB_SPRITES);
	instance_buffer_push(buffer, INSTANCE_BUFFER_SPRITE, instance);
	++job->sprites.count;
}

Texture_ID get_texture_id(Render* render, const char* filename)
{
	for (u32 i = 0; i < render->textures.len; ++i) {
		if (strcmp(filename, render->textures[i].filename) == 0) {
			return i;
		}
	}

	if (!has_space(render->textures)) {
		return INVALID_TEXTURE_ID;
	}
	Texture_ID tex_id = render->textures.len++;
	auto *new_tex = &render->textures.items[tex_id];
	strncpy(new_tex->filename, filename, ARRAY_SIZE(new_tex->filename));
	new_tex->texture = NULL;

	return tex_id;
}

JFG_Error load_textures(Render* render, Platform_Functions* platform_functions)
{
	for (u32 i = 0; i < render->textures.len; ++i) {
		auto texture = &render->textures[i];
		if (!texture->texture) {
			texture->texture = load_texture(texture->filename, platform_functions);
			if (!texture->texture) {
				jfg_set_error("Failed to load texture \"%s\"!", texture->filename);
				return JFG_ERROR;
			}
		}
	}

	return JFG_SUCCESS;
}