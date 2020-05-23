#define JFG_HLSL
#include "sprite_sheet_gpu_data_types.h"

// =============================================================================
// Sprite blitter

Sprite_Sheet_Constant_Buffer            constants : register(b0);
StructuredBuffer<Sprite_Sheet_Instance> instances : register(t0);
Texture2D<float4>                       tex       : register(t0);

struct VS_Sprite_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	uint   id         : SPRITE_ID;
	float  depth      : DEPTH;
};

struct PS_Sprite_Input
{
	float2 tex_coord  : TEXCOORD;
	uint   id         : SPRITE_ID;
	float  depth      : DEPTH;
};

struct PS_Sprite_Output
{
	float4 color : SV_Target0;
	uint   id    : SV_Target1;
	float  depth : SV_Depth;
};

struct VS_Sprite_Vertex
{
	float2 pos;
};

static const VS_Sprite_Vertex SPRITE_VERTICES[] = {
	{ 0.0f, 0.0f },
	{ 0.0f, 1.0f },
	{ 1.0f, 0.0f },

	{ 1.0f, 0.0f },
	{ 0.0f, 1.0f },
	{ 1.0f, 1.0f },
};

VS_Sprite_Output vs_sprite(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Sprite_Output output;

	VS_Sprite_Vertex      vertex   = SPRITE_VERTICES[vid];
	Sprite_Sheet_Instance instance = instances[iid];

	float2 sprite_size = constants.sprite_size / constants.screen_size;
	float2 pos = (vertex.pos + instance.world_pos) * sprite_size;
	pos = pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float2 sprite_pos = instance.sprite_pos + vertex.pos;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.tex_coord  = sprite_pos * constants.sprite_size  / constants.tex_size;
	output.id         = instance.sprite_id;
	output.depth      = (1.0f + instance.world_pos.y + instance.depth_offset) / 512.0f;

	return output;
}

PS_Sprite_Output ps_sprite(VS_Sprite_Output input)
{
	PS_Sprite_Output output;

	uint2 coord = floor(input.tex_coord * constants.tex_size);
	float4 tex_color = tex[coord];

	if (tex_color.a == 0.0f) {
		discard;
	}

	output.color = tex_color;
	output.id    = input.id;
	output.depth = input.depth;

	return output;
}

// =============================================================================
// Sprite ID clear

RWTexture2D<uint> sprite_id_tex : register(u0);

[numthreads(CLEAR_SPRITE_ID_WIDTH, CLEAR_SPRITE_ID_HEIGHT, 1)]
void cs_clear_sprite_id(uint2 tid : SV_DispatchThreadID)
{
	sprite_id_tex[tid] = 0;
}
