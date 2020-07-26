#define JFG_HLSL
#include "field_of_vision_render_gpu_data_types.h"

cbuffer blitter_constants : register(b0)
{
	Field_Of_Vision_Render_Constant_Buffer constants;
};

struct VS_Square_Vertex
{
	float2 pos;
};

static const VS_Square_Vertex SQUARE_VERTICES[] = {
	{  0.0f,  0.0f },
	{  0.0f,  1.0f },
	{  1.0f,  0.0f },

	{  1.0f,  0.0f },
	{  0.0f,  1.0f },
	{  1.0f,  1.0f },
};

// =============================================================================
// Edge

StructuredBuffer<Field_Of_Vision_Edge_Instance> edge_instances : register(t0);
Texture2D<float>                                edges          : register(t0);

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
	float4 color : SV_Target0;
};

VS_Edge_Output vs_edge(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Edge_Output output;

	VS_Square_Vertex vertex = SQUARE_VERTICES[vid];
	Field_Of_Vision_Edge_Instance instance = edge_instances[iid];

	float2 pos = (instance.world_coords + vertex.pos) / constants.grid_size;
	output.pos = float4(pos * float2(2.0f, -2.0f) - float2(1.0f, -1.0f), 0.0f, 1.0f);
	output.tex_coord = (instance.sprite_coords + vertex.pos) * constants.sprite_size / constants.edge_tex_size;

	return output;
}

PS_Edge_Output ps_edge(PS_Edge_Input input)
{
	PS_Edge_Output output;

	uint2 coord = floor(input.tex_coord * constants.edge_tex_size);
	output.color = 1.0f - edges[coord];

	return output;
}

// =============================================================================
// Fill

StructuredBuffer<Field_Of_Vision_Fill_Instance> fill_instances : register(t0);

struct VS_Fill_Output
{
	float4 pos        : SV_Position;
};

struct PS_Fill_Output
{
	float4 color : SV_Target0;
};

VS_Fill_Output vs_fill(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	VS_Fill_Output output;

	VS_Square_Vertex vertex = SQUARE_VERTICES[vid];
	Field_Of_Vision_Fill_Instance instance = fill_instances[iid];

	float2 pos = (instance.world_coords + vertex.pos) / constants.grid_size;
	output.pos = float4(pos * float2(2.0f, -2.0f) - float2(1.0f, -1.0f), 0.0f, 1.0f);

	return output;
}

PS_Fill_Output ps_fill()
{
	PS_Edge_Output output;
	output.color = 1.0f;
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

Texture2D<unorm float>    fov       : register(t0);
RWTexture2D<unorm float4> composite : register(u0);

[numthreads(FOV_COMPOSITE_WIDTH, FOV_COMPOSITE_HEIGHT, 1)]
void cs_composite(uint2 tid : SV_DispatchThreadID)
{
	float alpha = fov[tid];
	float4 color = composite[tid];
	composite[tid] = color * (1.0f + alpha) / 2.0f;
}
