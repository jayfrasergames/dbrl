#ifndef JFG_DEBUG_LINE_DRAW_H
#define JFG_DEBUG_LINE_DRAW_H

#include "stdafx.h"

#ifdef JFG_D3D11_H

#ifndef DEBUG_LINE_INCLUDE_GFX
#define DEBUG_LINE_INCLUDE_GFX
#endif

struct Debug_Line_D3D11
{
	ID3D11VertexShader       *vertex_shader;
	ID3D11PixelShader        *pixel_shader;
	ID3D11Buffer             *constants;
	ID3D11Buffer             *instances;
	ID3D11ShaderResourceView *instances_srv;
};

#endif

#include "debug_line_gpu_data_types.h"

#define DEBUG_LINE_MAX_LINES 1024

struct Debug_Line
{
	u32                        num_lines;
	Debug_Line_Instance        instances[DEBUG_LINE_MAX_LINES];
	Debug_Line_Constant_Buffer constants;

#ifdef DEBUG_LINE_INCLUDE_GFX
	union {
	#ifdef JFG_D3D11_H
		Debug_Line_D3D11 d3d11;
	#endif
	};
#endif
};

void debug_line_reset(Debug_Line* debug_line);
void debug_line_add_instance(Debug_Line* debug_line, Debug_Line_Instance instance);

#ifndef JFG_HEADER_ONLY

void debug_line_reset(Debug_Line* debug_line)
{
	debug_line->num_lines = 0;
}

void debug_line_add_instance(Debug_Line* debug_line, Debug_Line_Instance instance)
{
	ASSERT(debug_line->num_lines < DEBUG_LINE_MAX_LINES);
	debug_line->instances[debug_line->num_lines++] = instance;
}

#endif

#ifdef JFG_D3D11_H

u8 debug_line_d3d11_init(Debug_Line* debug_line, ID3D11Device* device);
void debug_line_d3d11_free(Debug_Line* debug_line);
void debug_line_d3d11_draw(Debug_Line*             debug_line,
                           ID3D11DeviceContext*    dc,
                           ID3D11RenderTargetView* output_rtv);

#ifndef JFG_HEADER_ONLY

#include "debug_line_dxbc_vertex_shader.data.h"
#include "debug_line_dxbc_pixel_shader.data.h"

u8 debug_line_d3d11_init(Debug_Line* debug_line, ID3D11Device* device)
{
	HRESULT hr;
	hr = device->CreateVertexShader(DEBUG_LINE_VS,
	                                ARRAY_SIZE(DEBUG_LINE_VS),
	                                NULL,
	                                &debug_line->d3d11.vertex_shader);
	if (FAILED(hr)) {
		goto error_init_vertex_shader;
	}

	hr = device->CreatePixelShader(DEBUG_LINE_PS,
	                               ARRAY_SIZE(DEBUG_LINE_PS),
	                               NULL,
	                               &debug_line->d3d11.pixel_shader);
	if (FAILED(hr)) {
		goto error_init_pixel_shader;
	}

	// init instance buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(debug_line->instances);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(debug_line->instances[0]);

		hr = device->CreateBuffer(&desc, NULL, &debug_line->d3d11.instances);
	}
	if (FAILED(hr)) {
		goto error_init_instances;
	}

	// init instance buffer srv
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		desc.Buffer.ElementOffset = 0;
		desc.Buffer.ElementWidth = DEBUG_LINE_MAX_LINES;

		hr = device->CreateShaderResourceView(debug_line->d3d11.instances,
		                                      &desc,
		                                      &debug_line->d3d11.instances_srv);
	}
	if (FAILED(hr)) {
		goto error_init_instances_srv;
	}

	// init constant buffer
	{
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(Debug_Line_Constant_Buffer);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		desc.StructureByteStride = sizeof(Debug_Line_Constant_Buffer);

		hr = device->CreateBuffer(&desc, NULL, &debug_line->d3d11.constants);
	}
	if (FAILED(hr)) {
		goto error_init_constants;
	}

	return 1;

	debug_line->d3d11.constants->Release();
error_init_constants:
	debug_line->d3d11.instances_srv->Release();
error_init_instances_srv:
	debug_line->d3d11.instances->Release();
error_init_instances:
	debug_line->d3d11.pixel_shader->Release();
error_init_pixel_shader:
	debug_line->d3d11.vertex_shader->Release();
error_init_vertex_shader:
	return 0;
}

void debug_line_d3d11_free(Debug_Line* debug_line)
{
	debug_line->d3d11.constants->Release();
	debug_line->d3d11.instances_srv->Release();
	debug_line->d3d11.instances->Release();
	debug_line->d3d11.pixel_shader->Release();
	debug_line->d3d11.vertex_shader->Release();
}

void debug_line_d3d11_draw(Debug_Line*             debug_line,
                           ID3D11DeviceContext*    dc,
                           ID3D11RenderTargetView* output_rtv)
{
	D3D11_MAPPED_SUBRESOURCE mapped_data = {};
	HRESULT hr;
	hr = dc->Map(debug_line->d3d11.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_data);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_data.pData, &debug_line->constants, sizeof(debug_line->constants));
	dc->Unmap(debug_line->d3d11.constants, 0);

	hr = dc->Map(debug_line->d3d11.instances, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_data);
	ASSERT(SUCCEEDED(hr));
	memcpy(mapped_data.pData,
	       &debug_line->instances,
	       sizeof(debug_line->instances[0]) * debug_line->num_lines);
	dc->Unmap(debug_line->d3d11.instances, 0);

	dc->IASetInputLayout(NULL);
	dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	dc->VSSetShader(debug_line->d3d11.vertex_shader, NULL, 0);
	dc->VSSetShaderResources(0, 1, &debug_line->d3d11.instances_srv);
	dc->VSSetConstantBuffers(0, 1, &debug_line->d3d11.constants);

	dc->PSSetShader(debug_line->d3d11.pixel_shader, NULL, 0);

	dc->DrawInstanced(2, debug_line->num_lines, 0, 0);
}

#endif // JFG_HEADER_ONLY
#endif // JFG_D3D11_H

#endif
