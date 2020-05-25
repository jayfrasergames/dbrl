#define JFG_HLSL
#include "sprite_sheet_gpu_data_types.h"

// =============================================================================
// Sprite blitter

cbuffer blitter_constants : register(b0)
{
	Sprite_Sheet_Constant_Buffer constants;
};
// ConstantBuffer<Sprite_Sheet_Constant_Buffer> constants : register(b0);
StructuredBuffer<Sprite_Sheet_Instance>      instances : register(t0);
Texture2D<float4>                            tex       : register(t0);

struct VS_Sprite_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	uint   id         : SPRITE_ID;
	float  depth      : DEPTH;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Sprite_Input
{
	float2 tex_coord  : TEXCOORD;
	uint   id         : SPRITE_ID;
	float  depth      : DEPTH;
	float4 color_mod  : COLOR_MOD;
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
	output.color_mod  = instance.color_mod;

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

	output.color = tex_color * input.color_mod;
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


// =============================================================================
// Sprite highlight ID

cbuffer highlight_constants : register(b0)
{
	Sprite_Sheet_Highlight_Constant_Buffer highlight_constants;
}
// ConstantBuffer<Sprite_Sheet_Highlight_Constant_Buffer> highlight_constants     : register(b0);
Texture2D<uint>                                        highlight_sprite_id_tex : register(t0);
RWTexture2D<float4>                                    highlight_color_tex     : register(u0);

[numthreads(HIGHLIGHT_SPRITE_WIDTH, HIGHLIGHT_SPRITE_HEIGHT, 1)]
void cs_highlight_sprite(uint2 tid : SV_DispatchThreadID)
{
	uint center = highlight_sprite_id_tex[tid];
	uint top    = highlight_sprite_id_tex[tid + uint2( 0, -1)];
	uint bottom = highlight_sprite_id_tex[tid + uint2( 0,  1)];
	uint left   = highlight_sprite_id_tex[tid + uint2(-1,  0)];
	uint right  = highlight_sprite_id_tex[tid + uint2( 1,  0)];
	if (center != highlight_constants.sprite_id
	    && (top    == highlight_constants.sprite_id ||
	        bottom == highlight_constants.sprite_id ||
	        left   == highlight_constants.sprite_id ||
	        right  == highlight_constants.sprite_id))
	{
		highlight_color_tex[tid] = highlight_constants.highlight_color;
	}
}
