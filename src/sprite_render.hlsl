#define JFG_HLSL
#include "sprite_render_gpu_data_types.h"

Sprite_Render_CB_Sprite                         sprite_constants  : register(b0);
StructuredBuffer<Sprite_Render_Sprite_Instance> sprite_instances  : register(t0);
Texture2D<float4>                           spriteset_texture : register(t0);

struct VS_Sprite_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	// float2 pixel_size : PIXEL_SIZE;
};

struct PS_Sprite_Input
{
	float2 tex_coord  : TEXCOORD;
	// float2 pixel_size : PIXEL_SIZE;
};

struct PS_Sprite_Output
{
	float4 color : SV_Target;
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

	VS_Sprite_Vertex            vertex   = SPRITE_VERTICES[vid];
	Sprite_Render_Sprite_Instance instance = sprite_instances[iid];

	float2 sprite_size = sprite_constants.sprite_size / sprite_constants.screen_size;
	float2 pos = (vertex.pos + instance.world_pos) * sprite_size;
	pos = sprite_constants.zoom * pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float2 sprite_pos = instance.sprite_pos + vertex.pos;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.tex_coord  = sprite_pos * sprite_constants.sprite_size  / sprite_constants.tex_size;

	return output;
}

PS_Sprite_Output ps_sprite(VS_Sprite_Output input)
{
	PS_Sprite_Output output;

	uint2 coord = floor(input.tex_coord * sprite_constants.tex_size);
	float4 tex_color = spriteset_texture[coord];

	if (tex_color.a == 0.0f) {
		discard;
	}

	output.color = tex_color;

	return output;
}
