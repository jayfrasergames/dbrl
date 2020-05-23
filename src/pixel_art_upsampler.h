#ifndef PIXEL_ART_UPSAMPLER_H
#define PIXEL_ART_UPSAMPLER_H

#include "jfg/prelude.h"

#include "pixel_art_upsampler_gpu_data_types.h"

#ifdef JFG_D3D11_H

#ifndef PIXEL_ART_UPSAMPLER_GFX_DEFINED
#define PIXEL_ART_UPSAMPLER_GFX_DEFINED
#endif

struct Pixel_Art_Upsampler_D3D11
{
	ID3D11Buffer*        constant_buffer;
	ID3D11ComputeShader* upsampler;
};
#endif

#ifdef PIXEL_ART_UPSAMPLER_GFX_DEFINED

struct Pixel_Art_Upsampler
{
	union {
	#ifdef JFG_D3D11_H
		Pixel_Art_Upsampler_D3D11 d3d11;
	#endif
	};
};

#endif

#ifdef JFG_D3D11_H
u8 pixel_art_upsampler_d3d11_init(Pixel_Art_Upsampler* pixel_art_upsampler, ID3D11Device* device);
void pixel_art_upsampler_d3d11_free(Pixel_Art_Upsampler* pixel_art_upsampler);

void pixel_art_upsampler_d3d11_draw(Pixel_Art_Upsampler*       pixel_art_upsampler,
                                    ID3D11DeviceContext*       dc,
                                    ID3D11ShaderResourceView*  input_srv,
                                    ID3D11UnorderedAccessView* output_uav,
                                    v2_u32 input_size,
                                    v2_u32 input_offset,
                                    v2_u32 output_size,
                                    v2_u32 output_offset);

#ifndef JFG_HEADER_ONLY
// XXX -- should define own assert
#include <assert.h>
#include "gen/pixel_art_upsampler_dxbc_compute_shader.data.h"

u8 pixel_art_upsampler_d3d11_init(Pixel_Art_Upsampler* pixel_art_upsampler, ID3D11Device* device)
{
	HRESULT hr;
	ID3D11Buffer *constant_buffer;
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Pixel_Art_Upsampler_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Pixel_Art_Upsampler_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &constant_buffer);
	}
	if (FAILED(hr)) {
		goto error_init_constant_buffer;
	}

	ID3D11ComputeShader *upsampler;
	hr = device->CreateComputeShader(PIXEL_ART_UPSAMPLER_CS,
	                                 ARRAY_SIZE(PIXEL_ART_UPSAMPLER_CS),
	                                 NULL,
	                                 &upsampler);
	if (FAILED(hr)) {
		goto error_init_upsampler;
	}

	pixel_art_upsampler->d3d11.constant_buffer = constant_buffer;
	pixel_art_upsampler->d3d11.upsampler       = upsampler;
	return 1;

	upsampler->Release();
error_init_upsampler:
	constant_buffer->Release();
error_init_constant_buffer:
	return 0;
}

void pixel_art_upsampler_d3d11_free(Pixel_Art_Upsampler* pixel_art_upsampler)
{
	pixel_art_upsampler->d3d11.upsampler->Release();
	pixel_art_upsampler->d3d11.constant_buffer->Release();
}

void pixel_art_upsampler_d3d11_draw(Pixel_Art_Upsampler*       pixel_art_upsampler,
                                    ID3D11DeviceContext*       dc,
                                    ID3D11ShaderResourceView*  input_srv,
                                    ID3D11UnorderedAccessView* output_uav,
                                    v2_u32 input_size,
                                    v2_u32 input_offset,
                                    v2_u32 output_size,
                                    v2_u32 output_offset)
{
	Pixel_Art_Upsampler_D3D11 *pau_d3d11 = &pixel_art_upsampler->d3d11;
	Pixel_Art_Upsampler_Constant_Buffer constant_buffer = {};
	constant_buffer.input_size    = input_size;
	constant_buffer.input_offset  = input_offset;
	constant_buffer.output_size   = output_size;
	constant_buffer.output_offset = output_offset;

	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE mapped_buffer;
	hr = dc->Map(pau_d3d11->constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
	assert(SUCCEEDED(hr));
	memcpy(mapped_buffer.pData, &constant_buffer, sizeof(constant_buffer));
	dc->Unmap(pau_d3d11->constant_buffer, 0);

	dc->CSSetUnorderedAccessViews(0, 1, &output_uav, NULL);
	dc->CSSetShaderResources(0, 1, &input_srv);
	dc->CSSetConstantBuffers(0, 1, &pau_d3d11->constant_buffer);
	dc->CSSetShader(pau_d3d11->upsampler, NULL, 0);

	dc->Dispatch((output_size.w + PIXEL_ART_UPSAMPLER_WIDTH  - 1) / PIXEL_ART_UPSAMPLER_WIDTH,
	             (output_size.h + PIXEL_ART_UPSAMPLER_HEIGHT - 1) / PIXEL_ART_UPSAMPLER_HEIGHT,
	             1);

	ID3D11UnorderedAccessView *null_uav = NULL;
	dc->CSSetUnorderedAccessViews(0, 1, &null_uav, NULL);
	ID3D11ShaderResourceView *null_srv = NULL;
	dc->CSSetShaderResources(0, 1, &null_srv);
	ID3D11Buffer *null_cb = NULL;
	dc->CSSetConstantBuffers(0, 1, &null_cb);
}

#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H


#endif
