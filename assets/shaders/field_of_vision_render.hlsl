#define JFG_HLSL
#include "field_of_vision_render_gpu_data_types.h"

cbuffer blitter_constants : register(b0)
{
	Field_Of_Vision_Render_Constant_Buffer constants;
};

static const float2 SQUARE_VERTICES[] = {
	float2(0.0f,  0.0f),
	float2(0.0f,  1.0f),
	float2(1.0f,  0.0f),

	float2(1.0f,  0.0f),
	float2(0.0f,  1.0f),
	float2(1.0f,  1.0f),
};

// =============================================================================
// Edge

Texture2D<float4> edges : register(t0);

struct VS_Edge_Output
{
	float2 tex_coord  : TEXCOORD;
	float4 pos        : SV_Position;
};

struct PS_Edge_Input
{
	float2 tex_coord  : TEXCOORD;
};

struct PS_Edge_Output
{
	uint color : SV_Target0;
};

VS_Edge_Output vs_edge(uint vid : SV_VertexID, Field_Of_Vision_Edge_Instance instance)
{
	VS_Edge_Output output;

	float2 vertex = SQUARE_VERTICES[vid];

	float2 pos = (instance.world_coords + vertex) / constants.grid_size;
	output.pos = float4(pos * float2(2.0f, -2.0f) - float2(1.0f, -1.0f), 0.0f, 1.0f);
	output.tex_coord = (instance.sprite_coords + vertex) * constants.sprite_size / constants.edge_tex_size;

	return output;
}

PS_Edge_Output ps_edge(PS_Edge_Input input)
{
	PS_Edge_Output output;

	uint2 coord = floor(input.tex_coord * constants.edge_tex_size);
	output.color = uint(1.0f - edges[coord].r) * constants.output_val;
	if (output.color == 0) {
		discard;
	}

	return output;
}

// =============================================================================
// Fill

struct VS_Fill_Output
{
	float4 pos        : SV_Position;
};

struct PS_Fill_Output
{
	uint color : SV_Target0;
};

VS_Fill_Output vs_fill(uint vid : SV_VertexID, Field_Of_Vision_Fill_Instance instance)
{
	VS_Fill_Output output;

	float2 vertex = SQUARE_VERTICES[vid];

	float2 pos = (instance.world_coords + vertex) / constants.grid_size;
	output.pos = float4(pos * float2(2.0f, -2.0f) - float2(1.0f, -1.0f), 0.0f, 1.0f);

	return output;
}

PS_Fill_Output ps_fill()
{
	PS_Fill_Output output;
	output.color = constants.output_val;
	return output;
}

// =============================================================================
// Shadow Blend Shader

cbuffer blend_constants : register(b0)
{
	Field_Of_Vision_Render_Blend_Constant_Buffer blend_constants;
};

Texture2D<unorm float>   blend_input  : register(t0);
RWTexture2D<unorm float> blend_output : register(u0);

[numthreads(FOV_SHADOW_BLEND_CS_WIDTH, FOV_SHADOW_BLEND_CS_HEIGHT, 1)]
void cs_shadow_blend(uint2 tid : SV_DispatchThreadID)
{
	float alpha = blend_constants.alpha;
	float input = blend_input[tid];
	float output = blend_output[tid];
	blend_output[tid] = input * alpha + output * (1.0f - alpha);
}

// =============================================================================
// Composite

Texture2D<uint>           fov           : register(t0);
Texture2D<unorm float4>   world_static  : register(t1);
Texture2D<unorm float4>   world_dynamic : register(t2);
RWTexture2D<unorm float4> world         : register(u0);

[numthreads(FOV_COMPOSITE_WIDTH, FOV_COMPOSITE_HEIGHT, 1)]
void cs_composite(uint2 tid : SV_DispatchThreadID)
{
	uint fov_val = fov[tid];

	switch (fov_val) {
	case 0: // NEVER SEEN
		world[tid] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		break;
	case 1: // PREVIOUSLY SEEN
		world[tid] = 0.75f * world_static[tid];
		break;
	case 2: { // VISIBLE
		float4 dyn = world_dynamic[tid];

		world[tid] = world_static[tid] * (1.0f - dyn.a) + dyn * dyn.a;
		break;
	}

	}
}

// =============================================================================
// Clear

RWTexture2D<uint> clear_target : register(u0);

[numthreads(FOV_CLEAR_WIDTH, FOV_CLEAR_HEIGHT, 1)]
void cs_clear(uint2 tid : SV_DispatchThreadID)
{
	clear_target[tid] = 0;
}