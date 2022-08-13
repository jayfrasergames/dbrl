#define JFG_HLSL
#include "triangle_gpu_data_types.h"

cbuffer global_cb : register(b0)
{
	Shader_Global_Constants global_constants;
};

// ==============================================================================
// Triangles

struct VS_Triangle_Output
{
	float4 pos   : SV_Position;
	float4 color : COLOR;
};

struct PS_Triangle_Input
{
	float4 color : COLOR;
};

struct PS_Triangle_Output
{
	float4 color : SV_Target0;
};

VS_Triangle_Output vs_triangle(uint vid : SV_VertexID, Triangle_Instance instance)
{
	VS_Triangle_Output output;

	float2 pos = float2(0.0f, 0.0f);

	switch (vid) {
	case 0: pos = instance.a; break;
	case 1: pos = instance.b; break;
	case 2: pos = instance.c; break;
	}

	pos /= global_constants.screen_size;
	pos = pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

	output.pos   = float4(pos, 0.0f, 1.0f);
	output.color = instance.color;

	return output;
}

PS_Triangle_Output ps_triangle(VS_Triangle_Output input)
{
	PS_Triangle_Output output;

	output.color = input.color;

	return output;
}