#define JFG_HLSL
#include "sprite_sheet_gpu_data_types.h"

// =============================================================================
// Sprite blitter

cbuffer blitter_constants : register(b0)
{
	Sprite_Sheet_Constant_Buffer constants;
};
Texture2D<float4>                            tex       : register(t0);

struct VS_Sprite_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	uint   id         : SPRITE_ID;
	float  depth      : DEPTH;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Sprite_Input
{
	float2 tex_coord  : TEXCOORD;
	uint   id         : SPRITE_ID;
	float  depth      : DEPTH;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Sprite_Output
{
	float4 color : SV_Target0;
	uint   id    : SV_Target1;
	float  depth : SV_Depth;
};

static const float2 SPRITE_VERTICES[] = {
	float2(0.0f,  0.0f),
	float2(0.0f,  1.0f),
	float2(1.0f,  0.0f),

	float2(1.0f,  0.0f),
	float2(0.0f,  1.0f),
	float2(1.0f,  1.0f),
};

VS_Sprite_Output vs_sprite(uint vid : SV_VertexID, Sprite_Sheet_Instance instance)
{
	VS_Sprite_Output output;

	float2 vertex = SPRITE_VERTICES[vid];

	float2 sprite_size = constants.sprite_size / constants.screen_size;
	float2 world_tile_size = constants.world_tile_size / constants.screen_size;
	float y_offset = instance.y_offset / constants.screen_size.y;

	// floa2 center = instance.world_pos * world_tile_size;

	float2 pos = vertex * sprite_size + instance.world_pos * world_tile_size
	           - (sprite_size - world_tile_size) / 2.0f;
	pos.y += y_offset;
	pos = pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
	float2 sprite_pos = instance.sprite_pos + vertex;

	output.pos        = float4(pos, 0.0f, 1.0f);
	output.tex_coord  = sprite_pos * constants.sprite_size  / constants.tex_size;
	output.id         = instance.sprite_id;
	output.depth      = (1.0f + instance.world_pos.y + instance.depth_offset) / 512.0f;
	output.color_mod  = instance.color_mod;

	return output;
}

PS_Sprite_Output ps_sprite(VS_Sprite_Output input)
{
	PS_Sprite_Output output;

	uint2 coord = floor(input.tex_coord * constants.tex_size);
	float4 tex_color = tex[coord];

	if (tex_color.a == 0.0f) {
		discard;
	}

	output.color = tex_color * input.color_mod;
	output.id    = input.id;
	output.depth = input.depth;

	return output;
}

// =============================================================================
// Sprite ID clear

RWTexture2D<uint> sprite_id_tex : register(u0);

[numthreads(CLEAR_SPRITE_ID_WIDTH, CLEAR_SPRITE_ID_HEIGHT, 1)]
void cs_clear_sprite_id(uint2 tid : SV_DispatchThreadID)
{
	sprite_id_tex[tid] = 0;
}


// =============================================================================
// Sprite highlight ID

cbuffer highlight_constants : register(b0)
{
	Sprite_Sheet_Highlight_Constant_Buffer highlight_constants;
}
// ConstantBuffer<Sprite_Sheet_Highlight_Constant_Buffer> highlight_constants     : register(b0);
Texture2D<uint>                                        highlight_sprite_id_tex : register(t0);
RWTexture2D<float4>                                    highlight_color_tex     : register(u0);

[numthreads(HIGHLIGHT_SPRITE_WIDTH, HIGHLIGHT_SPRITE_HEIGHT, 1)]
void cs_highlight_sprite(uint2 tid : SV_DispatchThreadID)
{
	uint center = highlight_sprite_id_tex[tid];
	uint top    = highlight_sprite_id_tex[tid + uint2( 0, -1)];
	uint bottom = highlight_sprite_id_tex[tid + uint2( 0,  1)];
	uint left   = highlight_sprite_id_tex[tid + uint2(-1,  0)];
	uint right  = highlight_sprite_id_tex[tid + uint2( 1,  0)];
	if (center != highlight_constants.sprite_id
	    && (top    == highlight_constants.sprite_id ||
	        bottom == highlight_constants.sprite_id ||
	        left   == highlight_constants.sprite_id ||
	        right  == highlight_constants.sprite_id))
	{
		highlight_color_tex[tid] = highlight_constants.highlight_color;
	}
}

// =============================================================================
// Font

StructuredBuffer<Sprite_Sheet_Font_Instance> font_instances : register(t0);
Texture2D<float4>                            font_tex       : register(t0);

struct VS_Font_Output
{
	float4 pos        : SV_Position;
	float2 tex_coord  : TEXCOORD;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Font_Input
{
	float2 tex_coord  : TEXCOORD;
	float4 color_mod  : COLOR_MOD;
};

struct PS_Font_Output
{
	float4 color : SV_Target0;
};

VS_Font_Output vs_font(uint vid : SV_VertexID, Sprite_Sheet_Font_Instance instance)
{
	VS_Font_Output output;

	float2 vertex = SPRITE_VERTICES[vid];

	float2 tex_coord = (instance.glyph_pos + vertex * instance.glyph_size) / constants.tex_size;
	float2 pos = (instance.world_pos * constants.world_tile_size
	              + instance.world_offset
	              + vertex * instance.glyph_size * instance.zoom) / constants.screen_size;
	pos = pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);

	output.color_mod = instance.color_mod;
	output.tex_coord = tex_coord;
	output.pos = float4(pos, 0.0f, 1.0f);

	return output;
}

PS_Font_Output ps_font(VS_Font_Output input)
{
	PS_Font_Output output;

	uint2 coord = floor(input.tex_coord * constants.tex_size);
	float4 tex_color = tex[coord];

	if (tex_color.a == 0.0f) {
		discard;
	}

	output.color = tex_color * input.color_mod;

	return output;
}
