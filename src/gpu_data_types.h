#ifndef GPU_DATA_TYPES_H
#define GPU_DATA_TYPES_H

#ifdef GPU
#define F1 float
#define F2 float2
#define F4 float4
#define U1 uint
#define U2 uint2
#else
#define F1 f32
#define F2 v2
#define F4 v4
#define U1 u32
#define U2 v2_u32
#endif

struct CB_Text
{
	F2 screen_size;
	F2 glyph_size;

	F2 tex_size;
	F1 zoom;
	F1 _dummy;
};

struct VS_Text_Instance
{
	U1 glyph;
	F2 pos;
	F4 color;
};

#undef V2
#undef V4
#undef U1

#endif
