#define JFG_HLSL
#include "triangle_gpu_data_types.h"

cbuffer global_cb : register(b0)
{
	Shader_Global_Constants global_constants;
};

cbuffer dispatch_cb : register (b1)
{
	Shader_Per_Dispatch_Constants dispatch_constants;
};

// ==============================================================================
// Triangles

StructuredBuffer<Triangle_Instance> triangles : register(t0);

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

VS_Triangle_Output vs_triangle(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Triangle_Output output;

	Triangle_Instance t = triangles[iid + dispatch_constants.base_offset];

	float2 pos = float2(0.0f, 0.0f);

	switch (vid) {
	case 0: pos = t.a; break;
	case 1: pos = t.b; break;
	case 2: pos = t.c; break;
	}

	pos /= global_constants.screen_size;
	pos = pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

	output.pos   = float4(pos, 0.0f, 1.0f);
	output.color = t.color;

	return output;
}

PS_Triangle_Output ps_triangle(VS_Triangle_Output input)
{
	PS_Triangle_Output output;

	output.color = input.color;

	return output;
}