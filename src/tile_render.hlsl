#define JFG_HLSL
#include "tile_render_gpu_data_types.h"

Tile_Render_CB_Tile                         tile_constants  : register(b0);
StructuredBuffer<Tile_Render_Tile_Instance> tile_instances  : register(t0);
Texture2D<float4>                           tileset_texture : register(t0);

struct VS_Tile_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	// float2 pixel_size : PIXEL_SIZE;
};

struct PS_Tile_Input
{
	float2 tex_coord  : TEXCOORD;
	// float2 pixel_size : PIXEL_SIZE;
};

struct PS_Tile_Output
{
	float4 color : SV_Target;
};

struct VS_Tile_Vertex
{
	float2 pos;
};

static const VS_Tile_Vertex TILE_VERTICES[] = {
	{ 0.0f, 0.0f },
	{ 0.0f, 1.0f },
	{ 1.0f, 0.0f },

	{ 1.0f, 0.0f },
	{ 0.0f, 1.0f },
	{ 1.0f, 1.0f },
};

VS_Tile_Output vs_tile(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Tile_Output output;

	VS_Tile_Vertex            vertex   = TILE_VERTICES[vid];
	Tile_Render_Tile_Instance instance = tile_instances[iid];

	float2 tile_size = tile_constants.tile_size / tile_constants.screen_size;
	float2 pos = (vertex.pos + instance.world_pos) * tile_size;
	pos = tile_constants.zoom * pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float2 sprite_pos = instance.sprite_pos + vertex.pos;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.tex_coord  = sprite_pos * tile_constants.tile_size  / tile_constants.tex_size;

	return output;
}

PS_Tile_Output ps_tile(VS_Tile_Output input)
{
	PS_Tile_Output output;

	uint2 coord = floor(input.tex_coord * tile_constants.tex_size);
	float4 tex_color = tileset_texture[coord];

	if (tex_color.a == 0.0f) {
		discard;
	}

	output.color = tex_color;

	return output;
}
