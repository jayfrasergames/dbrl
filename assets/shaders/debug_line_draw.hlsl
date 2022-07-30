#define JFG_HLSL
#include "debug_line_gpu_data_types.h"

cbuffer debug_line_constants : register(b0)
{
	Debug_Line_Constant_Buffer constants;
};
StructuredBuffer<Debug_Line_Instance> instances : register(t0);

struct VS_Output
{
	float4 pos        : SV_Position;
	float4 color      : COLOR;
};

struct PS_Input
{
	float4 color      : COLOR;
};

struct PS_Output
{
	float4 color : SV_Target;
};

VS_Output vs_line(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Output output;

	Debug_Line_Instance instance = instances[iid];

	float w = float(vid);
	float2 pos = instance.start * (1.0f - w) + instance.end * w;

	pos = (pos - constants.top_left) / (constants.bottom_right - constants.top_left);
	pos = 2.0f * pos - 1.0f;
	pos.y *= -1.0f;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.color      = instance.color;

	return output;
}

PS_Output ps_line(VS_Output input)
{
	PS_Output output;
	output.color = input.color;
	return output;
}
