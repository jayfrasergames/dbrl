#ifndef PIXEL_ART_UPSAMPLER_H
#define PIXEL_ART_UPSAMPLER_H

#include "prelude.h"

#include "jfg_d3d11.h"

#include "pixel_art_upsampler_gpu_data_types.h"

struct Pixel_Art_Upsampler_D3D11
{
	ID3D11Buffer*        constant_buffer;
	ID3D11ComputeShader* upsampler;
};

struct Pixel_Art_Upsampler
{
	union {
		Pixel_Art_Upsampler_D3D11 d3d11;
	};
};

u8 pixel_art_upsampler_d3d11_init(Pixel_Art_Upsampler* pixel_art_upsampler, ID3D11Device* device);
void pixel_art_upsampler_d3d11_free(Pixel_Art_Upsampler* pixel_art_upsampler);

void pixel_art_upsampler_d3d11_draw(Pixel_Art_Upsampler*       pixel_art_upsampler,
                                    ID3D11DeviceContext*       dc,
                                    ID3D11ShaderResourceView*  input_srv,
                                    ID3D11UnorderedAccessView* output_uav,
                                    v2 input_size,
                                    v2 input_offset,
                                    v2 output_size,
                                    v2 output_offset);

#endif
