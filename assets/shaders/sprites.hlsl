#define JFG_HLSL
#include "shader_global_constants.h"
#include "sprites_gpu_data_types.h"

// =============================================================================
// Sprite blitter

cbuffer cb : register(b0)
{
	Shader_Global_Constants global_constants;
};

cbuffer dispatch_cb : register(b1)
{
	Shader_Per_Dispatch_Constants dispatch_constants;
};

cbuffer cb2 : register(b2)
{
	Sprite_Constants constants;
};

Texture2D<float4>                 tex       : register(t0);

struct VS_Sprite_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Sprite_Input
{
	float2 tex_coord  : TEXCOORD;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Sprite_Output
{
	float4 color : SV_Target0;
};

static const float2 TRIANGLE_VERTICES[] = {
	float2(0.0f,  0.0f),
	float2(0.0f,  1.0f),
	float2(1.0f,  0.0f),

	float2(1.0f,  0.0f),
	float2(0.0f,  1.0f),
	float2(1.0f,  1.0f),
};

VS_Sprite_Output vs_sprite(uint vid : SV_VertexID, Sprite_Instance instance)
{
	VS_Sprite_Output output;

	float2 vertex = TRIANGLE_VERTICES[vid];

	float2 tex_coord = (instance.glyph_coords + vertex) * constants.tile_input_size;
	float2 pos = (instance.output_coords + vertex) * constants.tile_output_size / global_constants.screen_size;
	pos = pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

	output.color_mod = instance.color;
	output.tex_coord = tex_coord;
	output.pos = float4(pos, 0.0f, 1.0f);

	return output;
}

PS_Sprite_Output ps_sprite(VS_Sprite_Output input)
{
	PS_Sprite_Output output;

	uint2 coord = floor(input.tex_coord);
	float4 tex_color = tex[coord];

	output.color = tex_color * input.color_mod;

	return output;
}
