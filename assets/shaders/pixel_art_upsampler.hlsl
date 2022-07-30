#define JFG_HLSL
#include "pixel_art_upsampler_gpu_data_types.h"

// =============================================================================
// Upsampler

cbuffer Pixel_Art_Upsampler_Constants : register(b0)
{
	Pixel_Art_Upsampler_Constant_Buffer constants;
};
Texture2D<float4>                   input     : register(t0);
RWTexture2D<float4>                 output    : register(u0);

[numthreads(PIXEL_ART_UPSAMPLER_WIDTH, PIXEL_ART_UPSAMPLER_HEIGHT, 1)]
void cs_pixel_art_upsampler(int2 tid : SV_DispatchThreadID)
{
	float2 tidf = float2(tid);
	int2 sample_tl  = int2(constants.input_offset + (tidf * constants.input_size) / constants.output_size);
	int2 sample_br  = int2(constants.input_offset + ((tidf + 1.0f) * constants.input_size) / constants.output_size);
	float4 sample_00 = input[sample_tl];
	float4 sample_10 = input[int2(sample_br.x, sample_tl.y)];
	float4 sample_01 = input[int2(sample_tl.x, sample_br.y)];
	float4 sample_11 = input[sample_br];

	float2 input_size_float  = float2(constants.input_size);
	float2 output_size_float = float2(constants.output_size);
	float2 weight_tl = tidf * (input_size_float / output_size_float);
	float2 weight_br = (tidf + 1.0f) * (input_size_float / output_size_float);
	float2 center = floor(weight_br);
	float2 weight = min((weight_br - center) / (weight_br - weight_tl), 1.0f);

	output[tid] = (1.0f - weight.x) * (1.0f - weight.y) * sample_00
	            +         weight.x  * (1.0f - weight.y) * sample_10
	            + (1.0f - weight.x) *         weight.y  * sample_01
	            +         weight.x  *         weight.y  * sample_11;
}
