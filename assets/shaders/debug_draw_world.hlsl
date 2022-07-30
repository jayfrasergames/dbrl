#define JFG_HLSL
#include "debug_draw_world_gpu_data_types.h"

cbuffer debug_draw_world_contants : register(b0)
{
	Debug_Draw_World_Constant_Buffer constants;
};

// ==============================================================================
// Triangles

StructuredBuffer<Debug_Draw_World_Triangle> triangles : register(t0);

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

	Debug_Draw_World_Triangle t = triangles[iid];

	float2 pos;

	switch (vid) {
	case 0: pos = t.a; break;
	case 1: pos = t.b; break;
	case 2: pos = t.c; break;
	}

	pos += constants.center;
	pos /= constants.zoom;
	pos = pos * 2.0f; // - 1.0f;
	pos.y *= -1.0f;
	// pos.x *= -1.0f;

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

// ==============================================================================
// Lines

StructuredBuffer<Debug_Draw_World_Line> lines : register(t0);

struct VS_Line_Output
{
	float4 pos   : SV_Position;
	float4 color : COLOR;
};

struct PS_Line_Input
{
	float4 color;
};

struct PS_Line_Output
{
	float4 color : SV_Target0;
};

VS_Line_Output vs_line(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Line_Output output;

	Debug_Draw_World_Line l = lines[iid];

	float2 pos;

	switch (vid) {
	case 0: pos = l.start; break;
	case 1: pos = l.end;   break;
	}

	pos += constants.center;
	pos /= constants.zoom;
	pos = pos * 2.0f; // - 1.0f;
	pos.y *= -1.0f;
	// pos.x *= -1.0f;

	output.pos   = float4(pos, 0.0f, 1.0f);
	output.color = l.color;

	return output;
}

PS_Line_Output ps_line(VS_Line_Output input)
{
	PS_Line_Output output;

	output.color = input.color;

	return output;
}
