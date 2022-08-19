#define JFG_HLSL
#include "debug_draw_world_gpu_data_types.h"

cbuffer debug_draw_world_contants : register(b0)
{
	Debug_Draw_World_Constant_Buffer constants;
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

VS_Triangle_Output vs_triangle(uint vid : SV_VertexID, Debug_Draw_World_Triangle instance)
{
	VS_Triangle_Output output;

	float2 pos = float2(0.0f, 0.0f);

	switch (vid) {
	case 0: pos = instance.a; break;
	case 1: pos = instance.b; break;
	case 2: pos = instance.c; break;
	}

	pos += constants.center;
	pos /= constants.zoom;
	pos = pos * 2.0f; // - 1.0f;
	pos.y *= -1.0f;
	// pos.x *= -1.0f;

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

// ==============================================================================
// Lines

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

VS_Line_Output vs_line(uint vid : SV_VertexID, Debug_Draw_World_Line instance)
{
	VS_Line_Output output;

	float2 pos = float2(0.0f, 0.0f);

	switch (vid) {
	case 0: pos = instance.start; break;
	case 1: pos = instance.end;   break;
	}

	pos += constants.center;
	pos /= constants.zoom;
	pos = pos * 2.0f; // - 1.0f;
	pos.y *= -1.0f;
	// pos.x *= -1.0f;

	output.pos   = float4(pos, 0.0f, 1.0f);
	output.color = instance.color;

	return output;
}

PS_Line_Output ps_line(VS_Line_Output input)
{
	PS_Line_Output output;

	output.color = input.color;

	return output;
}
