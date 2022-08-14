#pragma once

#include "prelude.h"
#include "texture.h"
#include "containers.hpp"
#include "jfg_error.h"

#include "types.h"

#include "sprites_gpu_data_types.h"
#include "triangle_gpu_data_types.h"
#include "field_of_vision_render_gpu_data_types.h"
#include "sprite_sheet_gpu_data_types.h"
#include "particles_gpu_data_types.h"

#define MAX_EVENT_DEPTH 32
// EVENT(name)
#define RENDER_EVENTS \
	EVENT(WORLD) \
	EVENT(WORLD_STATIC) \
	EVENT(WORLD_DYNAMIC) \
	EVENT(FOV) \
	EVENT(FOV_PRECOMPUTE) \
	EVENT(FOV_COMPOSITE)

enum Render_Event
{
#define EVENT(name) RENDER_EVENT_##name,
	RENDER_EVENTS
#undef EVENT
};

#define FORMATS \
	FORMAT(R32_F) \
	FORMAT(R32G32_F) \
	FORMAT(R32G32B32A32_F) \
	FORMAT(R8G8B8A8_UNORM) \
	FORMAT(R8_U) \
	FORMAT(R32_U) \
	FORMAT(D16_UNORM)

enum Format
{
#define FORMAT(name) FORMAT_##name,
	FORMATS
#undef FORMAT
};

enum Texture_Bind_Flags
{
	BIND_SRV = 1<<0,
	BIND_UAV = 1<<1,
	BIND_RTV = 1<<2,
	BIND_DSV = 1<<3,
};

// TODO -- bind flags for SRV/UAV/RTV
// TARGET_TEXTURE(name, width, height, format, bind_flags)
#define TARGET_TEXTURES \
	TARGET_TEXTURE(FOV_RENDER,      24*256, 24*256, FORMAT_R8_U,           BIND_SRV | BIND_UAV | BIND_RTV) \
	TARGET_TEXTURE(SPRITE_ID,       24*256, 24*256, FORMAT_R32_U,          BIND_SRV | BIND_UAV | BIND_RTV) \
	TARGET_TEXTURE(SPRITE_DEPTH,    24*256, 24*256, FORMAT_D16_UNORM,      BIND_DSV) \
	TARGET_TEXTURE(WORLD_STATIC,    24*256, 24*256, FORMAT_R8G8B8A8_UNORM, BIND_SRV | BIND_UAV | BIND_RTV) \
	TARGET_TEXTURE(WORLD_DYNAMIC,   24*256, 24*256, FORMAT_R8G8B8A8_UNORM, BIND_SRV | BIND_UAV | BIND_RTV) \
	TARGET_TEXTURE(WORLD_COMPOSITE, 24*256, 24*256, FORMAT_R8G8B8A8_UNORM, BIND_SRV | BIND_UAV | BIND_RTV)

enum Target_Texture_ID
{
#define TARGET_TEXTURE(name, ...) TARGET_TEXTURE_##name,
	TARGET_TEXTURES
#undef TARGET_TEXTURE

	NUM_TARGET_TEXTURES
};

struct Target_Texture_Metadata
{
	const char         *name;
	v2_u32              size;
	Format              format;
	Texture_Bind_Flags  bind_flags;
};

extern Target_Texture_Metadata TARGET_TEXTURE_METADATA[NUM_TARGET_TEXTURES];

// TEXTURE(name, filename)
#define SOURCE_TEXTURES \
	TEXTURE(CODEPAGE437, "Codepage-437.png") \
	TEXTURE(EDGES,       "water_edges.png") \
	TEXTURE(TILES,       "tiles.png") \
	TEXTURE(CREATURES,   "creatures.png") \
	TEXTURE(EFFECTS_24,  "effects24.png") \
	TEXTURE(EFFECTS_32,  "effects32.png") \
	TEXTURE(BOXY_BOLD,   "boxy_font.png")

enum Source_Texture_ID
{
#define TEXTURE(name, ...) SOURCE_TEXTURE_##name,
	SOURCE_TEXTURES
#undef TEXTURE

	NUM_SOURCE_TEXTURES
};

enum Render_Job_Type
{
	RENDER_JOB_NONE,
	RENDER_JOB_TRIANGLES,
	RENDER_JOB_SPRITES,
	RENDER_JOB_FOV,
	RENDER_JOB_FOV_COMPOSITE,
	RENDER_JOB_CLEAR_RTV,
	RENDER_JOB_CLEAR_DEPTH,
	RENDER_JOB_CLEAR_UINT,
	RENDER_JOB_WORLD_SPRITE,
	RENDER_JOB_WORLD_FONT,
	RENDER_JOB_WORLD_HIGHLIGHT_SPRITE_ID,
	RENDER_JOB_WORLD_PARTICLES,
	RENDER_JOB_BEGIN_EVENT,
	RENDER_JOB_END_EVENT,

	RENDER_JOB_XXX_FLUSH_OLD_RENDERER,
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
			Source_Texture_ID sprite_sheet_id;
			Sprite_Constants  constants;
			u32               start;
			u32               count;
		} sprites;
		struct {
			Target_Texture_ID output_tex_id;
			Field_Of_Vision_Render_Constant_Buffer constants;
			u32 edge_start;
			u32 edge_count;
			u32 fill_start;
			u32 fill_count;
		} fov;
		struct {
			Target_Texture_ID tex_id;
		} clear_uint;
		struct {
			Target_Texture_ID tex_id;
			v4                clear_value;
		} clear_rtv;
		struct {
			Target_Texture_ID tex_id;
			f32               value;
		} clear_depth;
		struct {
			Target_Texture_ID            output_tex_id;
			Target_Texture_ID            output_sprite_id_tex_id;
			Target_Texture_ID            depth_tex_id;
			Source_Texture_ID            sprite_tex_id;
			Sprite_Sheet_Constant_Buffer constants;
			u32                          start;
			u32                          count;
		} world_sprite;
		struct {
			Target_Texture_ID            output_tex_id;
			Source_Texture_ID            font_tex_id;
			Sprite_Sheet_Constant_Buffer constants;
			u32                          start;
			u32                          count;
		} world_font;
		struct {
			Target_Texture_ID         output_tex_id;
			Particles_Constant_Buffer constants;
			u32                       start;
			u32                       count;
		} world_particles;
		struct {
			Target_Texture_ID output_tex_id;
			Target_Texture_ID sprite_id_tex_id;
			Entity_ID         sprite_id;
			v4                color;
		} world_highlight;
		struct {
			Render_Event event;
		} begin_event;
		struct {
			Render_Event event;
		} end_event;
		struct {
			Target_Texture_ID fov_id;
			Target_Texture_ID world_static_id;
			Target_Texture_ID world_dynamic_id;
			Target_Texture_ID output_tex_id;
		} fov_composite;
	};
};

extern const char *SOURCE_TEXTURE_FILENAMES[NUM_SOURCE_TEXTURES];

// INSTANCE_BUFFER(name, type, max_elems)
#define INSTANCE_BUFFERS \
	INSTANCE_BUFFER(SPRITE,       Sprite_Instance,               8192) \
	INSTANCE_BUFFER(WORLD_SPRITE, Sprite_Sheet_Instance,         8192) \
	INSTANCE_BUFFER(WORLD_FONT,   Sprite_Sheet_Font_Instance,    8192) \
	INSTANCE_BUFFER(TRIANGLE,     Triangle_Instance,             2048) \
	INSTANCE_BUFFER(FOV_EDGE,     Field_Of_Vision_Edge_Instance, 256*256) \
	INSTANCE_BUFFER(FOV_FILL,     Field_Of_Vision_Fill_Instance, 256*256) \
	INSTANCE_BUFFER(PARTICLES,    Particle_Instance,             4096)

// TODO -- offset?
// INPUT_ELEM(instance_buffer_name, semantic_name, format)
#define INSTANCE_BUFFER_INPUT_ELEMS \
	INPUT_ELEM(SPRITE, "GLYPH_COORDS",  FORMAT_R32G32_F) \
	INPUT_ELEM(SPRITE, "OUTPUT_COORDS", FORMAT_R32G32_F) \
	INPUT_ELEM(SPRITE, "COLOR",         FORMAT_R32G32B32A32_F) \
	\
	INPUT_ELEM(TRIANGLE, "VERTEX_A", FORMAT_R32G32_F) \
	INPUT_ELEM(TRIANGLE, "VERTEX_B", FORMAT_R32G32_F) \
	INPUT_ELEM(TRIANGLE, "VERTEX_C", FORMAT_R32G32_F) \
	INPUT_ELEM(TRIANGLE, "COLOR",    FORMAT_R32G32B32A32_F) \
	\
	INPUT_ELEM(FOV_EDGE, "WORLD_COORDS",  FORMAT_R32G32_F) \
	INPUT_ELEM(FOV_EDGE, "SPRITE_COORDS", FORMAT_R32G32_F) \
	\
	INPUT_ELEM(FOV_FILL, "WORLD_COORDS",  FORMAT_R32G32_F) \
	\
	INPUT_ELEM(WORLD_SPRITE, "SPRITE_POS",   FORMAT_R32G32_F) \
	INPUT_ELEM(WORLD_SPRITE, "WORLD_POS",    FORMAT_R32G32_F) \
	INPUT_ELEM(WORLD_SPRITE, "SPRITE_ID",    FORMAT_R32_U) \
	INPUT_ELEM(WORLD_SPRITE, "DEPTH_OFFSET", FORMAT_R32_F) \
	INPUT_ELEM(WORLD_SPRITE, "Y_OFFSET",     FORMAT_R32_F) \
	INPUT_ELEM(WORLD_SPRITE, "COLOR_MOD",    FORMAT_R32G32B32A32_F) \
	\
	INPUT_ELEM(WORLD_FONT, "GLYPH_POS",      FORMAT_R32G32_F) \
	INPUT_ELEM(WORLD_FONT, "GLYPH_SIZE",     FORMAT_R32G32_F) \
	INPUT_ELEM(WORLD_FONT, "WORLD_POS",      FORMAT_R32G32_F) \
	INPUT_ELEM(WORLD_FONT, "WORLD_OFFSET",   FORMAT_R32G32_F) \
	INPUT_ELEM(WORLD_FONT, "ZOOM",           FORMAT_R32_F) \
	INPUT_ELEM(WORLD_FONT, "COLOR_MOD",      FORMAT_R32G32B32A32_F) \
	\
	INPUT_ELEM(PARTICLES, "START_TIME",       FORMAT_R32_F) \
	INPUT_ELEM(PARTICLES, "END_TIME",         FORMAT_R32_F) \
	INPUT_ELEM(PARTICLES, "START_POS",        FORMAT_R32G32_F) \
	INPUT_ELEM(PARTICLES, "START_COLOR",      FORMAT_R32G32B32A32_F) \
	INPUT_ELEM(PARTICLES, "END_COLOR",        FORMAT_R32G32B32A32_F) \
	INPUT_ELEM(PARTICLES, "START_VELOCITY",   FORMAT_R32G32_F) \
	INPUT_ELEM(PARTICLES, "ACCELERATION",     FORMAT_R32G32_F) \
	INPUT_ELEM(PARTICLES, "SIN_INNER_COEFF",  FORMAT_R32G32_F) \
	INPUT_ELEM(PARTICLES, "SIN_OUTER_COEFF",  FORMAT_R32G32_F) \
	INPUT_ELEM(PARTICLES, "SIN_PHASE_OFFSET", FORMAT_R32G32_F)


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
	const char *name;
	size_t      size;
	size_t      element_size;
	u32         max_elems;
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
	Stack<Render_Event, MAX_EVENT_DEPTH> event_stack;
};

struct Render
{
	v2_u32 screen_size;
	Texture *textures[NUM_SOURCE_TEXTURES];
	Render_Job_Buffer render_job_buffer;
};

JFG_Error init(Render* render);
JFG_Error load_textures(Render* render, Platform_Functions* platform_functions);

void reset(Render_Job_Buffer* buffer);
Render_Job* next_job(Render_Job_Buffer* buffer);

void push_triangle(Render_Job_Buffer* buffer, Triangle_Instance instance);

void begin_sprites(Render_Job_Buffer* buffer, Source_Texture_ID texture_id, Sprite_Constants constants);
void push_sprite(Render_Job_Buffer* buffer, Sprite_Instance instance);

void begin_fov(Render_Job_Buffer* buffer, Target_Texture_ID output_tex_id, Field_Of_Vision_Render_Constant_Buffer constants);
void push_fov_fill(Render_Job_Buffer* buffer, Field_Of_Vision_Fill_Instance instance);
void push_fov_edge(Render_Job_Buffer* buffer, Field_Of_Vision_Edge_Instance instance);

void clear_uint(Render_Job_Buffer* buffer, Target_Texture_ID tex_id);
void clear_rtv(Render_Job_Buffer* buffer, Target_Texture_ID tex_id, v4 clear_value);
void clear_depth(Render_Job_Buffer* buffer, Target_Texture_ID tex_id, f32 value);

struct Render_Push_World_Sprites_Desc
{
	Target_Texture_ID            output_tex_id;
	Target_Texture_ID            output_sprite_id_tex_id;
	Target_Texture_ID            depth_tex_id;
	Source_Texture_ID            sprite_tex_id;
	Sprite_Sheet_Constant_Buffer constants;
	Slice<Sprite_Sheet_Instance> instances;
};
void push_world_sprites(Render_Job_Buffer* buffer, Render_Push_World_Sprites_Desc* desc);

struct Render_Push_World_Font_Desc
{
	Target_Texture_ID                 output_tex_id;
	Source_Texture_ID                 font_tex_id;
	Sprite_Sheet_Constant_Buffer      constants;
	Slice<Sprite_Sheet_Font_Instance> instances;
};
void push_world_font(Render_Job_Buffer* buffer, Render_Push_World_Font_Desc* desc);

struct Render_Push_World_Particles_Desc
{
	Target_Texture_ID        output_tex_id;
	f32                      time;
	v2                       world_size;
	v2                       tile_size;
	Slice<Particle_Instance> instances;
};
void push_world_particles(Render_Job_Buffer* buffer, Render_Push_World_Particles_Desc* desc);

void begin(Render_Job_Buffer* buffer, Render_Event event);
void end(Render_Job_Buffer* buffer, Render_Event event);

void push(Render_Job_Buffer* buffer, Render_Job job);

void highlight_sprite_id(Render_Job_Buffer* buffer, Target_Texture_ID output_tex_id, Target_Texture_ID sprite_id_tex_id, Entity_ID sprite_id, v4 color);