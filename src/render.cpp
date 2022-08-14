#include "render.h"

#include "stdafx.h"

#define RENDER_JOB_BUFFER_SIZE 1024*1024

Instance_Buffer_Metadata INSTANCE_BUFFER_METADATA[] = {
#define INSTANCE_BUFFER(name, type, max_elems) { #name, sizeof(type) * max_elems, sizeof(type), max_elems },
	INSTANCE_BUFFERS
#undef INSTANCE_BUFFER
};

Instance_Buffer_Input_Elem_Metadata INSTANCE_BUFFER_INPUT_ELEM_METADATA[] = {
#define INPUT_ELEM(instance_buffer_id, semantic_name, format) { INSTANCE_BUFFER_##instance_buffer_id, semantic_name, format },
	INSTANCE_BUFFER_INPUT_ELEMS
#undef INPUT_ELEM
};

Target_Texture_Metadata TARGET_TEXTURE_METADATA[] = {
#define TARGET_TEXTURE(name, width, height, format, bind_flags) { "TARGET_" #name, v2_u32(width, height), format, (Texture_Bind_Flags)(bind_flags) },
	TARGET_TEXTURES
#undef TARGET_TEXTURE
};

const char *SOURCE_TEXTURE_FILENAMES[] = {
#define TEXTURE(name, filename) filename,
	SOURCE_TEXTURES
#undef TEXTURE
};

JFG_Error init(Render* render)
{
	memset(render, 0, sizeof(*render));

	return JFG_SUCCESS;
}

void reset(Render_Job_Buffer* buffer)
{
	buffer->jobs.reset();
	buffer->event_stack.reset();
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

template <typename T>
static inline void instance_buffer_push_slice(Render_Job_Buffer* buffer, Instance_Buffer_ID ib_id, Slice<T> instances)
{
	auto ib = &buffer->instance_buffers[ib_id];
	ASSERT(ib->cur_pos + instances.len <= INSTANCE_BUFFER_METADATA[ib_id].max_elems);
	size_t size = instances.len * sizeof(instances[0]);
	memcpy(ib->buffer_head, instances.base, size);
	ib->buffer_head = (void*)((uptr)ib->buffer_head + size);
	ib->cur_pos += instances.len;
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

void begin_sprites(Render_Job_Buffer* buffer, Source_Texture_ID texture_id, Sprite_Constants constants)
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

void begin_fov(Render_Job_Buffer* buffer, Target_Texture_ID output_tex_id, Field_Of_Vision_Render_Constant_Buffer constants)
{
	auto job = buffer->jobs.append();
	job->type = RENDER_JOB_FOV;
	job->fov.output_tex_id = output_tex_id;
	job->fov.constants = constants;
	job->fov.edge_start = buffer->instance_buffers[INSTANCE_BUFFER_FOV_EDGE].cur_pos;
	job->fov.edge_count = 0;
	job->fov.fill_start = buffer->instance_buffers[INSTANCE_BUFFER_FOV_FILL].cur_pos;
	job->fov.fill_count = 0;
}

void push_fov_fill(Render_Job_Buffer* buffer, Field_Of_Vision_Fill_Instance instance)
{
	auto job = cur_job(buffer);
	ASSERT(job && job->type == RENDER_JOB_FOV);
	instance_buffer_push(buffer, INSTANCE_BUFFER_FOV_FILL, instance);
	++job->fov.fill_count;
}

void push_fov_edge(Render_Job_Buffer* buffer, Field_Of_Vision_Edge_Instance instance)
{
	auto job = cur_job(buffer);
	ASSERT(job && job->type == RENDER_JOB_FOV);
	instance_buffer_push(buffer, INSTANCE_BUFFER_FOV_EDGE, instance);
	++job->fov.edge_count;
}

void clear_uint(Render_Job_Buffer* buffer, Target_Texture_ID tex_id)
{
	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_CLEAR_UINT;
	job->clear_uint.tex_id = tex_id;
}

void clear_depth(Render_Job_Buffer* buffer, Target_Texture_ID tex_id, f32 value)
{
	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_CLEAR_DEPTH;
	job->clear_depth.tex_id = tex_id;
	job->clear_depth.value = value;
}

void push_world_sprites(Render_Job_Buffer* buffer, Render_Push_World_Sprites_Desc* desc)
{
	if (desc->instances.len == 0) {
		return;
	}

	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_WORLD_SPRITE;
	job->world_sprite.output_tex_id = desc->output_tex_id;
	job->world_sprite.output_sprite_id_tex_id = desc->output_sprite_id_tex_id;
	job->world_sprite.depth_tex_id = desc->depth_tex_id;
	job->world_sprite.sprite_tex_id = desc->sprite_tex_id;
	job->world_sprite.constants = desc->constants;

	job->world_sprite.start = buffer->instance_buffers[INSTANCE_BUFFER_WORLD_SPRITE].cur_pos;
	job->world_sprite.count = desc->instances.len;

	instance_buffer_push_slice(buffer, INSTANCE_BUFFER_WORLD_SPRITE, desc->instances);
}

void push_world_font(Render_Job_Buffer* buffer, Render_Push_World_Font_Desc* desc)
{
	if (desc->instances.len == 0) {
		return;
	}

	auto job = buffer->jobs.append();
	
	job->type = RENDER_JOB_WORLD_FONT;
	job->world_font.output_tex_id = desc->output_tex_id;
	job->world_font.font_tex_id = desc->font_tex_id;
	job->world_font.constants = desc->constants;

	job->world_font.start = buffer->instance_buffers[INSTANCE_BUFFER_WORLD_FONT].cur_pos;
	job->world_font.count = desc->instances.len;

	instance_buffer_push_slice(buffer, INSTANCE_BUFFER_WORLD_FONT, desc->instances);
}

void push_world_particles(Render_Job_Buffer* buffer, Render_Push_World_Particles_Desc* desc)
{
	if (desc->instances.len == 0) {
		return;
	}

	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_WORLD_PARTICLES;
	job->world_particles.output_tex_id = desc->output_tex_id;
	job->world_particles.constants.time = desc->time;
	job->world_particles.constants.world_size = desc->world_size;
	job->world_particles.constants.tile_size = desc->tile_size;

	job->world_particles.start = buffer->instance_buffers[INSTANCE_BUFFER_PARTICLES].cur_pos;
	job->world_particles.count = desc->instances.len;

	instance_buffer_push_slice(buffer, INSTANCE_BUFFER_PARTICLES, desc->instances);
}

void clear_rtv(Render_Job_Buffer* buffer, Target_Texture_ID tex_id, v4 clear_value)
{
	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_CLEAR_RTV;
	job->clear_rtv.tex_id      = tex_id;
	job->clear_rtv.clear_value = clear_value;
}

void begin(Render_Job_Buffer* buffer, Render_Event event)
{
	buffer->event_stack.push(event);

	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_BEGIN_EVENT;
	job->begin_event.event = event;
}

void end(Render_Job_Buffer* buffer, Render_Event event)
{
	auto old_event = buffer->event_stack.pop();
	ASSERT(event == old_event);

	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_END_EVENT;
	job->end_event.event = event;
}

void push(Render_Job_Buffer* buffer, Render_Job job)
{
	buffer->jobs.append(job);
}

void highlight_sprite_id(Render_Job_Buffer* buffer, Target_Texture_ID output_tex_id, Target_Texture_ID sprite_id_tex_id, Entity_ID sprite_id, v4 color)
{
	auto job = buffer->jobs.append();

	job->type = RENDER_JOB_WORLD_HIGHLIGHT_SPRITE_ID;
	job->world_highlight.output_tex_id = output_tex_id;
	job->world_highlight.sprite_id_tex_id = sprite_id_tex_id;
	job->world_highlight.sprite_id = sprite_id;
	job->world_highlight.color = color;
}


JFG_Error load_textures(Render* render, Platform_Functions* platform_functions)
{
	for (u32 i = 0; i < NUM_SOURCE_TEXTURES; ++i) {
		auto filename = SOURCE_TEXTURE_FILENAMES[i];

		// TODO -- make this reload friendly
		auto texture = render->textures[i];
		if (!texture) {
			texture = load_texture(filename, platform_functions);
			if (!texture) {
				jfg_set_error("Failed to load texture \"%s\"!", filename);
				return JFG_ERROR;
			}
		}
		render->textures[i] = texture;
	}

	return JFG_SUCCESS;
}

static int l_reload_shaders(lua_State* lua_state)
{
	i32 num_args = lua_gettop(lua_state);
	if (num_args != 0) {
		return luaL_error(lua_state, "Lua function 'reload_shaders' expected 0 arguments, got %d!", num_args);
	}
	auto render = (Render*)lua_touserdata(lua_state, lua_upvalueindex(1));
	auto log = (Log*)lua_touserdata(lua_state, lua_upvalueindex(2));
	auto buffer = &render->render_job_buffer;

	auto job = buffer->jobs.append();
	job->type = RENDER_JOB_RELOAD_SHADERS;
	job->reload_shaders.log = log;

	return 0;
}

void register_lua_functions(Render* render, Log* log, lua_State* lua_state)
{
	lua_pushlightuserdata(lua_state, render);
	lua_pushlightuserdata(lua_state, log);
	lua_pushcclosure(lua_state, l_reload_shaders, 2);
	lua_setglobal(lua_state, "reload_shaders");
}