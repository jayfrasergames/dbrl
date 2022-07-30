#define JFG_HLSL
#include "imgui_gpu_data_types.h"

IMGUI_CB_Text                            text_constants : register(b0);
StructuredBuffer<IMGUI_VS_Text_Instance> text_instances : register(t0);
Texture2D<float>                         font_texture   : register(t0);

struct VS_Text_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	// float2 pixel_size : PIXEL_SIZE;
	float4 color      : COLOR;
};

struct PS_Text_Input
{
	float2 tex_coord  : TEXCOORD;
	// float2 pixel_size : PIXEL_SIZE;
	float4 color      : COLOR;
};

struct PS_Text_Output
{
	float4 color : SV_Target;
};

struct VS_Text_Vertex
{
	float2 pos;
};

static const VS_Text_Vertex TEXT_VERTICES[] = {
	{ 0.0f, 0.0f },
	{ 0.0f, 1.0f },
	{ 1.0f, 0.0f },

	{ 1.0f, 0.0f },
	{ 0.0f, 1.0f },
	{ 1.0f, 1.0f },
};

VS_Text_Output vs_text(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Text_Output output;

	VS_Text_Vertex         vertex   = TEXT_VERTICES[vid];
	IMGUI_VS_Text_Instance instance = text_instances[iid];

	float2 glyph_size = text_constants.glyph_size / text_constants.screen_size;
	float2 pos = (vertex.pos + instance.pos) * glyph_size;
	pos = text_constants.zoom * pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float2 glyph_pos = float2(instance.glyph % 32, instance.glyph >> 5) + vertex.pos;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.tex_coord  = glyph_pos * text_constants.glyph_size  / text_constants.tex_size;
	// output.pixel_size = text_constants.glyph_size / glyph_size;
	output.color      = instance.color;

	return output;
}

PS_Text_Output ps_text(VS_Text_Output input)
{
	PS_Text_Output output;

	uint2 coord = floor(input.tex_coord * text_constants.tex_size);
	float tex_color = font_texture[coord];

	if (tex_color == 0.0f) {
		discard;
	}

	output.color = tex_color * input.color;

	return output;
}
