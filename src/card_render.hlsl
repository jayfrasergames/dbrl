#define JFG_HLSL
#include "card_render_gpu_data_types.h"

cbuffer blitter_constants : register(b0)
{
	Card_Render_Constant_Buffer constants;
};
StructuredBuffer<Card_Render_Instance> instances : register(t0);
Texture2D<float4>                      tex       : register(t0);

struct VS_Card_Output
{
	float4 pos        : SV_Position;
	float4 color_mod  : COLOR_MOD;
	float2 tex_coord  : TEXCOORD;
};

struct PS_Card_Input
{
	float4 color_mod  : COLOR_MOD;
	float2 tex_coord  : TEXCOORD;
};

struct PS_Card_Output
{
	float4 color : SV_Target0;
};

struct VS_Card_Vertex
{
	float2 pos;
};

static const VS_Card_Vertex CARD_VERTICES[] = {
	{ -1.0f, -1.0f },
	{ -1.0f,  1.0f },
	{  1.0f, -1.0f },

	{  1.0f, -1.0f },
	{ -1.0f,  1.0f },
	{  1.0f,  1.0f },
};

VS_Card_Output vs_card(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Card_Output output;

	VS_Card_Vertex       vertex   = CARD_VERTICES[vid];
	Card_Render_Instance instance = instances[iid];
	instance.screen_pos.x *= constants.screen_size.y / constants.screen_size.x;

	// float2 card_size = constants.card_size / constants.screen_size;
	float2 card_size = float2(constants.card_size.x / constants.card_size.y, 1.0f);

	float2 pos = instance.zoom * 0.5f * constants.zoom * vertex.pos * card_size;

	float ch = cos(instance.horizontal_rotation);
	float cv = cos(instance.vertical_rotation);
	pos *= float2(ch, cv);

	float front_facing = 0.5f + 0.5f * sign(ch) * sign(cv);

	float c = cos(instance.screen_rotation);
	float s = sin(instance.screen_rotation);
	pos = mul(float2x2(c, -s, s, c), pos);

	pos.x *= constants.screen_size.y / constants.screen_size.x;
	// pos /= constants.screen_size;

	pos += instance.screen_pos;
	float2 card_pos = front_facing * instance.card_pos + (vertex.pos * float2(1.0f, -1.0f) + 1.0f) / 2.0f;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.tex_coord  = card_pos * constants.card_size / constants.tex_size;
	output.color_mod  = instance.color_mod;

	return output;
}

PS_Card_Output ps_card(VS_Card_Output input)
{
	PS_Card_Output output;

	uint2 coord = floor(input.tex_coord * constants.tex_size);
	float4 tex_color = tex[coord];

	if (tex_color.a == 0.0f) {
		discard;
	}

	output.color = input.color_mod * tex_color;

	return output;
}
